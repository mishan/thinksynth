/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * clock.js -- three clocks and the two maps between them (JAM_M3.md,
 * section 6).
 *
 * The relay's clock is what a room agrees on: Play names an origin in
 * relay milliseconds, and every peer turns that into a frame of its own
 * audio output. The audio clock is the one the transport is driven by, and
 * it is the only clock that ever times a note. The wall clock,
 * performance.now(), is the go-between, because it is what the relay is
 * pinged with and what the audio clock reports itself against.
 *
 * Nothing here reads a clock. Every sample is handed in, so the page and
 * the protocol harness -- which has no relay, no audio device and a wall
 * clock of its own making -- run the same code over the same arithmetic.
 */

/* How many samples each map keeps. A ping a second, so about the last
   quarter minute: enough to have seen a quiet round trip, few enough that
   a clock that drifted is followed. */
const KEEP = 16;

/* Offset to the relay's clock, from ping samples.
 *
 * NTP's idea: of the last few round trips, believe the shortest one. A
 * ping that took longer than the others was held up somewhere, and the
 * offset it implies is off by however unevenly it was held up on the two
 * legs; the shortest one had the least room to be wrong. */
export class RelayClock
{
    constructor ()
    {
        this.samples = [];
    }

    /* A ping sent at local `t0', answered with the relay's `t1', the
       answer received at local `t2'. Milliseconds throughout. */
    sample (t0, t1, t2)
    {
        this.samples.push({ offset: t1 - (t0 + t2) / 2, rtt: t2 - t0 });

        if (this.samples.length > KEEP)
            this.samples.shift();
    }

    get count ()
    {
        return this.samples.length;
    }

    /* The sample believed: the one with the shortest round trip. */
    get best ()
    {
        let best = null;

        for (const s of this.samples)
            if (best === null || s.rtt < best.rtt)
                best = s;

        return best;
    }

    /* relay = local + offset. NaN until there is a sample. */
    get offset ()
    {
        return this.best?.offset ?? NaN;
    }

    get rtt ()
    {
        return this.best?.rtt ?? NaN;
    }

    /* How far apart the kept offsets are: the measure of how much to
       trust the one believed. Under a millisecond on a LAN. */
    get spread ()
    {
        if (this.samples.length === 0)
            return NaN;

        let lo = Infinity, hi = -Infinity;

        for (const s of this.samples)
        {
            lo = Math.min(lo, s.offset);
            hi = Math.max(hi, s.offset);
        }

        return hi - lo;
    }

    relayOf (localMs)
    {
        return localMs + this.offset;
    }

    localOf (relayMs)
    {
        return relayMs - this.offset;
    }
}

/* The audio clock against the wall clock: a straight line fitted through
 * the last few pairs the context reported together, so that a wall-clock
 * time can be turned into the frame the output will be at then.
 *
 * A line rather than the last pair, because the two clocks are two
 * crystals and drift apart by tens of parts per million -- a millisecond
 * in a minute, which is a window. And a fit rather than the last pair
 * because a pair is reported to the nearest quantum. */
export class AudioClock
{
    constructor (sampleRate)
    {
        this.rate = sampleRate;
        this.samples = [];      /* { x: performance ms, y: context seconds } */
    }

    /* getOutputTimestamp()'s two numbers, as they came. */
    sample (contextTime, performanceTime)
    {
        this.samples.push({ x: performanceTime, y: contextTime });

        if (this.samples.length > KEEP)
            this.samples.shift();

        this.fit = null;
    }

    get count ()
    {
        return this.samples.length;
    }

    /* Least squares through the samples; with one, a line of the nominal
       slope through it. Cached until the next sample. */
    line ()
    {
        if (this.fit !== null && this.fit !== undefined)
            return this.fit;

        const n = this.samples.length;

        if (n === 0)
            return null;

        /* Around the mean, so the sums stay small and the slope is not
           a difference of two large numbers. */
        let mx = 0, my = 0;

        for (const s of this.samples)
        {
            mx += s.x;
            my += s.y;
        }

        mx /= n;
        my /= n;

        let sxx = 0, sxy = 0;

        for (const s of this.samples)
        {
            sxx += (s.x - mx) * (s.x - mx);
            sxy += (s.x - mx) * (s.y - my);
        }

        const slope = sxx > 0 ? sxy / sxx : 1 / 1000;

        this.fit = { mx, my, slope };

        return this.fit;
    }

    /* The context's time at a wall-clock moment, in seconds. NaN with no
       sample. */
    contextTimeAt (performanceMs)
    {
        const f = this.line();

        return f === null ? NaN : f.my + (performanceMs - f.mx) * f.slope;
    }

    frameAt (performanceMs)
    {
        return this.contextTimeAt(performanceMs) * this.rate;
    }

    /* The worst the line misses a sample by, in seconds: how much to
       trust a frame it gives. A quantum or so is a good fit. */
    get residual ()
    {
        const f = this.line();

        if (f === null)
            return NaN;

        let worst = 0;

        for (const s of this.samples)
            worst = Math.max(worst, Math.abs(this.contextTimeAt(s.x) - s.y));

        return worst;
    }
}

/* The transport, as the page knows it: from the worklet's tape messages,
 * which carry where transport zero is as a frame, and the audio clock,
 * which says where the output has got to.
 *
 * Transport time is (frame - origin) / rate exactly, on the worklet and
 * here, so between two messages the page's reading is not an estimate: it
 * is the same subtraction the worklet does, on the same clock. */
export class TransportClock
{
    constructor (sampleRate)
    {
        this.rate = sampleRate;
        this.origin = -1;
        this.running = false;
        this.reported = 0;      /* the worklet's `now' in its last message */
    }

    /* A tape message. */
    report ({ now, origin, running })
    {
        this.reported = now;
        this.origin = origin;
        this.running = running;
    }

    /* Transport seconds now, given the context's current time. While the
       transport is stopped, where it stopped. */
    now (contextTime)
    {
        if (!this.running || this.origin < 0)
            return this.reported;

        return contextTime - this.origin / this.rate;
    }

    /* A transport time as a frame of this peer's output. */
    frameOf (at)
    {
        return this.origin + at * this.rate;
    }
}

/* The two maps end to end: an origin in relay milliseconds to a frame of
   this peer's output. What a `transport start' is applied through. */
export function frameOfRelayMs (relayMs, relayClock, audioClock)
{
    return audioClock.frameAt(relayClock.localOf(relayMs));
}
