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
 * clock.js -- three clocks and the two maps between them.
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

/* The audio clock against the wall clock, so that a wall-clock time can
 * be turned into the frame the output will be at then.
 *
 * Both run in seconds, so the line between them has a slope of one, give
 * or take the tens of parts per million two crystals disagree by -- a
 * millisecond a minute, and the window below renews the estimate every
 * few seconds anyway. So what is estimated is the offset alone, as the
 * median of the last few pairs the context reported together.
 *
 * The median and not a fitted line, found the hard way: a least-squares
 * line through the same pairs put a Chromium peer's origin 400 ms from a
 * Firefox peer's. A pair reported while the output stream was still
 * starting up -- context time at zero, wall clock already moving -- sits
 * far off the line, tilts it, and a tilt of a few percent over a
 * sixteen-second window is hundreds of milliseconds at the origin. A
 * median does not see it. */
export class AudioClock
{
    constructor (sampleRate)
    {
        this.rate = sampleRate;
        this.offsets = [];      /* contextTime - performanceTime / 1000 */
    }

    /* getOutputTimestamp()'s two numbers, as they came. A context that
       has not started ticking reports zero, and that is not a sample. */
    sample (contextTime, performanceTime)
    {
        if (!(contextTime > 0))
            return;

        this.offsets.push(contextTime - performanceTime / 1000);

        if (this.offsets.length > KEEP)
            this.offsets.shift();
    }

    get count ()
    {
        return this.offsets.length;
    }

    get offset ()
    {
        if (this.offsets.length === 0)
            return NaN;

        const sorted = [...this.offsets].sort((a, b) => a - b);
        const mid = sorted.length >> 1;

        return sorted.length % 2 === 1 ? sorted[mid]
                                       : (sorted[mid - 1] + sorted[mid]) / 2;
    }

    /* The context's time at a wall-clock moment, in seconds. NaN with no
       sample. */
    contextTimeAt (performanceMs)
    {
        return performanceMs / 1000 + this.offset;
    }

    frameAt (performanceMs)
    {
        return this.contextTimeAt(performanceMs) * this.rate;
    }

    /* The worst a kept sample is from the offset believed, in seconds:
       how much to trust a frame it gives. A quantum or so is good. */
    get residual ()
    {
        const offset = this.offset;

        if (Number.isNaN(offset))
            return NaN;

        let worst = 0;

        for (const o of this.offsets)
            worst = Math.max(worst, Math.abs(o - offset));

        return worst;
    }
}

/* The transport, as the page knows it: from the worklet's tape messages,
 * which carry where transport zero is as a frame and how fast the clock
 * is running, and the audio clock, which says where the output has got
 * to.
 *
 * Transport time is (frame - origin) * speed / rate exactly, on the
 * worklet and here, so between two messages the page's reading is not an
 * estimate: it is the same arithmetic the worklet does, on the same
 * clock.
 *
 * The speed is a multiple of real time and a solo page's slider moves it
 * (thinkweb.cpp, tw_speed). It is 1 in a room -- nothing shares it yet --
 * but it is read here rather than assumed, because the two numbers are
 * the whole of the line and a clock that knew one of them would be wrong
 * by a factor with nothing to say so. */
export class TransportClock
{
    constructor (sampleRate)
    {
        this.rate = sampleRate;
        this.origin = -1;
        this.speed = 1;
        this.running = false;
        this.reported = 0;      /* the worklet's `now' in its last message */
        this.reportedAt = NaN;  /* the wall clock when that message came */
    }

    /* A tape message, and the wall clock as it arrived. */
    report ({ now, origin, running, speed = 1 }, wallMs = NaN)
    {
        this.reported = now;
        this.reportedAt = wallMs;
        this.origin = origin;
        this.speed = speed > 0 ? speed : 1;
        this.running = running;
    }

    /* Transport seconds now, given the context's current time and the
       wall clock. While the transport is stopped, where it stopped.
     *
     * Two readings, the fresher one. The context's current time is the
     * worklet's own clock, but read from the main thread it can be
     * stale -- some fifty milliseconds in headless Firefox -- and a stamp
     * made from a stale reading is earlier than it means to be, which
     * eats the lead a knob is sent with. The last tape message plus the
     * wall clock since is stale only by the message's own trip. Both
     * advance at the speed the clock is turned to, so the larger of the
     * two is still the less stale. */
    now (contextTime, wallMs = NaN)
    {
        if (!this.running || this.origin < 0)
            return this.reported;

        const fromContext =
            (contextTime - this.origin / this.rate) * this.speed;

        if (Number.isNaN(wallMs) || Number.isNaN(this.reportedAt))
            return fromContext;

        return Math.max(
            fromContext,
            this.reported +
                (wallMs - this.reportedAt) / 1000 * this.speed);
    }

    /* A transport time as a frame of this peer's output. A transport
       second is rate/speed frames, which is the whole difference between
       this and the multiplication it used to be. */
    frameOf (at)
    {
        return this.origin + at * this.rate / this.speed;
    }
}

/* The two maps end to end: an origin in relay milliseconds to a frame of
   this peer's output. What a `transport start' is applied through. */
export function frameOfRelayMs (relayMs, relayClock, audioClock)
{
    return audioClock.frameAt(relayClock.localOf(relayMs));
}
