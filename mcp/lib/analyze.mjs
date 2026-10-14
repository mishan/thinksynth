// What a render sounds like, said in numbers a model can reason about:
// loudness as ITU-R BS.1770 measures it, brightness, where the energy sits,
// how busy it is, and -- from the stems -- which instrument is buried under
// which and which two are fighting over one band. A model cannot hear; these
// are the things a listener would have told it.

// ---- loudness ---------------------------------------------------------------

// BS.1770's K-weighting at any rate: the shelf and the high-pass, designed
// as pyloudnorm and libebur128 design them, so 44.1 kHz is measured the way
// 48 kHz is.
function biquad (kind, fc, q, gainDb, rate)
{
    const A = 10 ** (gainDb / 40), w0 = 2 * Math.PI * fc / rate;
    const cw = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), sA = Math.sqrt(A);
    let b, a;

    if (kind === 'shelf')
    {
        b = [A * ((A + 1) + (A - 1) * cw + 2 * sA * alpha),
             -2 * A * ((A - 1) + (A + 1) * cw),
             A * ((A + 1) + (A - 1) * cw - 2 * sA * alpha)];
        a = [(A + 1) - (A - 1) * cw + 2 * sA * alpha,
             2 * ((A - 1) - (A + 1) * cw),
             (A + 1) - (A - 1) * cw - 2 * sA * alpha];
    }
    else
    {
        b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2];
        a = [1 + alpha, -2 * cw, 1 - alpha];
    }

    return { b: b.map(v => v / a[0]), a: [1, a[1] / a[0], a[2] / a[0]] };
}

function filter (x, { b, a }, y = new Float32Array(x.length))
{
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    for (let i = 0; i < x.length; i++)
    {
        const v = b[0] * x[i] + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2;

        x2 = x1; x1 = x[i]; y2 = y1; y1 = v;
        y[i] = v;
    }

    return y;
}

// The K-weighted energy of every 10 ms step, summed over channels, as prefix
// sums; a 400 ms block stepped by 100 ms is a difference of two, and so is a
// window shorter than a block. A sum per sample would be twice the audio's
// own size again.
function blocks (channels, rate)
{
    const shelf = biquad('shelf', 1681.974450955533, 0.7071752369554196,
                         3.999843853973347, rate);
    const hp = biquad('hp', 38.13547087602444, 0.5003270373238773, 0, rate);
    const step = Math.max(1, Math.round(0.01 * rate));
    const steps = Math.floor(channels[0].length / step);
    const sums = new Float64Array(steps + 1), list = [];

    for (const c of channels)
    {
        const w = filter(c, shelf);

        filter(w, hp, w);

        for (let k = 0; k < steps; k++)
        {
            let z = 0;

            for (let i = k * step; i < (k + 1) * step; i++)
                z += w[i] * w[i];

            sums[k + 1] += z;
        }
    }

    for (let k = 0; k < steps; k++)
        sums[k + 1] += sums[k];

    for (let k = 0; k + 40 <= steps; k += 10)
        list.push({ at: k * step / rate, z: (sums[k + 40] - sums[k]) / (40 * step) });

    return { list, sums, step };
}

const lufsOf = (z) => -0.691 + 10 * Math.log10(z);

// Integrated loudness of the blocks that start in [t0, t1): gated at -70
// LUFS and then 10 LU under the ungated mean, as BS.1770 says. Null where
// nothing passes the gate -- silence has no loudness.
function integrated (list, t0 = 0, t1 = Infinity)
{
    const zs = list.filter(b => b.at >= t0 && b.at < t1 && lufsOf(b.z) > -70)
                   .map(b => b.z);

    if (!zs.length)
        return null;

    const rel = lufsOf(zs.reduce((s, z) => s + z, 0) / zs.length) - 10;
    const kept = zs.filter(z => lufsOf(z) > rel);

    return kept.length
        ? +lufsOf(kept.reduce((s, z) => s + z, 0) / kept.length).toFixed(1)
        : null;
}

// A window shorter than a block, ungated: a bar at a fast tempo is.
function shortLoudness ({ sums, step }, rate, t0, t1)
{
    const k0 = Math.max(0, Math.floor(t0 * rate / step));
    const k1 = Math.min(sums.length - 1, Math.floor(t1 * rate / step));

    if (k1 <= k0)
        return null;

    const l = lufsOf((sums[k1] - sums[k0]) / ((k1 - k0) * step));

    return l > -70 ? +l.toFixed(1) : null;
}

// ---- the spectrum -----------------------------------------------------------

const FFT = 4096, HOP = 2048;

function fft (re, im)
{
    const n = re.length;

    for (let i = 1, j = 0; i < n; i++)
    {
        let bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
        {
            [re[i], re[j]] = [re[j], re[i]];
            [im[i], im[j]] = [im[j], im[i]];
        }
    }

    for (let len = 2; len <= n; len <<= 1)
    {
        const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);

        for (let i = 0; i < n; i += len)
        {
            let cr = 1, ci = 0;

            for (let k = 0; k < len / 2; k++)
            {
                const a = i + k, b = a + len / 2;
                const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;

                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;

                const nr = cr * wr - ci * wi;

                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

const BANDS = [['low', 0, 250], ['mid', 250, 2000], ['high', 2000, Infinity]];

// One frame every HOP samples of the channels' sum: its brightness, its
// energy per band, and how much it rose from the frame before (the flux an
// onset is a peak of).
function frames (channels, rate)
{
    const n = channels[0].length, win = new Float32Array(FFT);
    const out = [];
    let prev = null;

    for (let i = 0; i < FFT; i++)
        win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (FFT - 1));

    for (let at = 0; at + FFT <= n; at += HOP)
    {
        const re = new Float64Array(FFT), im = new Float64Array(FFT);

        for (let i = 0; i < FFT; i++)
        {
            let v = 0;

            for (const c of channels)
                v += c[at + i];

            re[i] = v / channels.length * win[i];
        }

        fft(re, im);

        const mag = new Float64Array(FFT / 2);
        const band = { low: 0, mid: 0, high: 0 };
        let sum = 0, weighted = 0, flux = 0;

        for (let k = 1; k < FFT / 2; k++)
        {
            const f = k * rate / FFT, m = Math.hypot(re[k], im[k]);

            mag[k] = m;
            sum += m;
            weighted += m * f;

            for (const [name, lo, hi] of BANDS)
                if (f >= lo && f < hi)
                    band[name] += m * m;

            if (prev)
                flux += Math.max(0, m - prev[k]);
        }

        out.push({ at: (at + FFT / 2) / rate, centroid: sum > 1e-9 ? weighted / sum : 0,
                   band, flux, energy: sum });
        prev = mag;
    }

    return out;
}

// Peaks in the flux that stand above their neighborhood: struck notes,
// roughly, which is what "busy" means to a listener.
function onsets (fr)
{
    const hits = [];

    for (let i = 1; i + 1 < fr.length; i++)
    {
        const lo = Math.max(0, i - 8), hi = Math.min(fr.length, i + 9);
        let mean = 0;

        for (let k = lo; k < hi; k++)
            mean += fr[k].flux;

        mean /= hi - lo;

        if (fr[i].flux > fr[i - 1].flux && fr[i].flux >= fr[i + 1].flux &&
            fr[i].flux > mean * 1.5 && fr[i].energy > 1e-3)
            hits.push(fr[i].at);
    }

    return hits;
}

function spectral (fr, t0, t1)
{
    const inside = fr.filter(f => f.at >= t0 && f.at < t1 && f.energy > 1e-3);

    if (!inside.length)
        return null;

    const total = { low: 0, mid: 0, high: 0 };
    let c = 0, w = 0;

    for (const f of inside)
    {
        for (const b in total)
            total[b] += f.band[b];

        c += f.centroid * f.energy;
        w += f.energy;
    }

    const all = total.low + total.mid + total.high || 1;
    const shares = Object.fromEntries(
        Object.entries(total).map(([k, v]) => [k, +(v / all).toFixed(2)]));

    return { centroid: Math.round(c / w),
             bands: shares,
             main: Object.entries(shares).sort((a, b) => b[1] - a[1])[0][0] };
}

// ---- one signal, measured ---------------------------------------------------

export function measure ({ channels, rate })
{
    const blk = blocks(channels, rate);
    const fr = frames(channels, rate);

    let peak = 0;

    for (const c of channels)
        for (let i = 0; i < c.length; i++)
            peak = Math.max(peak, Math.abs(c[i]));

    return { rate, seconds: channels[0].length / rate, peak, blk, fr,
             hits: onsets(fr), channels };
}

function peakIn (m, t0, t1)
{
    const i0 = Math.max(0, Math.floor(t0 * m.rate));
    const i1 = Math.min(m.channels[0].length, Math.floor(t1 * m.rate));
    let p = 0;

    for (const c of m.channels)
        for (let i = i0; i < i1; i++)
            p = Math.max(p, Math.abs(c[i]));

    return p;
}

export function describe (m, t0, t1)
{
    const loud = t1 - t0 >= 0.4 ? integrated(m.blk.list, t0, t1 - 0.4 + 1e-9)
                                : shortLoudness(m.blk, m.rate, t0, t1);
    const peak = peakIn(m, t0, t1);

    return {
        lufs: loud,
        peakDb: peak > 0 ? +(20 * Math.log10(peak)).toFixed(1) : null,
        ...(spectral(m.fr, t0, t1) ?? {}),
        onsetsPerSecond: +(m.hits.filter(t => t >= t0 && t < t1).length /
                           (t1 - t0)).toFixed(2),
    };
}

export function wholeLoudness (m)
{
    return integrated(m.blk.list);
}

// ---- windows and findings ---------------------------------------------------

// Bars, a fixed length, or the sections as they played -- the last from the
// render's own timeline, so a cued or looping arrangement is measured where
// it actually went. The timeline is in the written audio's time, and a
// section's `pieceAt' is where it began in the piece, which is what its bar
// number counts from.
export function windows (kind, { seconds, tempo, meter, timeline, from = 0 })
{
    const out = [];

    if (kind === 'section' && timeline?.length)
    {
        timeline.forEach((t, i) =>
        {
            const end = i + 1 < timeline.length ? timeline[i + 1].at : seconds;

            if (end > t.at)
                out.push({ label: `${t.section}, bar ${Math.round((t.pieceAt ?? t.at) * tempo / 60 / meter) + 1}`,
                           start: t.at, end });
        });

        return out;
    }

    // Bars on the piece's bar lines, numbered as the piece numbers them,
    // whatever --from cut the audio at.
    if (kind === 'bar')
    {
        const bar = meter * 60 / tempo;

        for (let at = 0; at < seconds - 1e-6; )
        {
            const n = Math.floor((at + from) / bar + 1e-6);
            const end = Math.min(seconds, (n + 1) * bar - from);

            out.push({ label: `bar ${n + 1}`, start: at, end });
            at = end;
        }

        return out;
    }

    const step = typeof kind === 'number' ? kind : 4 * 60 / tempo * meter;

    for (let at = 0; at < seconds - 1e-6; at += step)
        out.push({ label: `${at.toFixed(1)} s`, start: at, end: Math.min(seconds, at + step) });

    return out;
}

const BURIED_LU = 18, CLASH_LU = 6, CLASH_RATIO = 2 ** (1 / 3);

// What a listener would say about one window, from its stems: who is lost
// under whom, and which two are in each other's way.
export function findings (stems)
{
    const on = stems.filter(s => s.lufs !== null && s.lufs > -60);
    const out = [];

    if (on.length < 2)
        return out;

    const top = on.reduce((a, b) => (b.lufs > a.lufs ? b : a));

    for (const s of on)
        if (s !== top && top.lufs - s.lufs > BURIED_LU)
            out.push(`${s.instrument} is ${(top.lufs - s.lufs).toFixed(0)} LU ` +
                     `under ${top.instrument}`);

    for (let i = 0; i < on.length; i++)
        for (let k = i + 1; k < on.length; k++)
        {
            const a = on[i], b = on[k];

            if (!a.centroid || !b.centroid)
                continue;

            const ratio = Math.max(a.centroid, b.centroid) / Math.min(a.centroid, b.centroid);

            if (Math.abs(a.lufs - b.lufs) <= CLASH_LU && ratio <= CLASH_RATIO &&
                a.main === b.main)
                out.push(`${a.instrument} and ${b.instrument} share the ` +
                         `${a.main} band around ${Math.round(Math.sqrt(a.centroid * b.centroid))} Hz ` +
                         `at about the same level`);
        }

    return out;
}

// A finding said once, with every stretch of consecutive windows it holds
// in.
export function collapse (perWindow)
{
    const runs = new Map();

    for (const w of perWindow)
        for (const f of w.findings)
        {
            const list = runs.get(f) ?? [];
            const last = list[list.length - 1];

            if (last && last.index === w.index - 1)
                Object.assign(last, { to: w.label, index: w.index });
            else
                list.push({ from: w.label, to: w.label, index: w.index });

            runs.set(f, list);
        }

    return [...runs.entries()].map(([f, list]) =>
        `${f} (${list.map(r => (r.from === r.to ? r.from : `${r.from} to ${r.to}`)).join('; ')})`);
}
