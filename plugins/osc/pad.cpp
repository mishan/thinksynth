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

/* PADsynth: partials with a bandwidth each, rendered once into a table.
 *
 * A saw's partials are lines, one frequency each, and a stack of detuned
 * saws is a handful of lines each. A choir, a string section, the lush
 * pad of every ambient record is not lines but bands: sixty violins are
 * sixty slightly different pitches, and what the ear hears at the second
 * harmonic is a smear of energy a few cents wide. Paul Nasca's PADsynth
 * builds that directly. For every partial n it lays a Gaussian into a
 * magnitude spectrum,
 *
 *     centered on  base * n * sqrt(1 + stretch * n^2)
 *     width        (2^(bandwidth / 1200) - 1) * base * n^bwscale  Hz
 *     height       tilt dB per octave above the fundamental
 *
 * gives every bin a random phase, and inverse-transforms the lot. What
 * comes back is one long table -- 2^18 samples, six seconds at 44.1 kHz
 * -- whose every partial is already a band, and which loops without a
 * seam because an inverse FFT is periodic in its own length. Played back
 * by `freq' it is a pad with no LFO and no detune in it, that never
 * repeats in any way the ear can find.
 *
 *   partials   how many
 *   bandwidth  each band's width at the fundamental, in cents; 0 is a
 *              line, and the table is then a sum of sines
 *   bwscale    how the width grows up the series: 1 keeps it the same
 *              number of cents on every partial, 0 the same number of
 *              hertz, which is narrower and more like one instrument
 *   tilt       dB per octave; -6 is a saw's slope, -12 a triangle's
 *   stretch    inharmonicity, the stiffness of a string: 0 is harmonic
 *
 * ONE TABLE PER OCTAVE. A table is rendered at a base pitch and read
 * faster or slower for every other, and reading it an octave up doubles
 * every partial in it -- so a table with partials up to Nyquist at its
 * base would fold them back down at the top of the keyboard. Each voice
 * picks the octave nearest its first `freq', a table is rendered for that
 * octave's C with only the partials that stay under Nyquist half an
 * octave above it, and the voice reads that one from then on. Two voices
 * in one octave with the same params share one table.
 *
 * THE PARAMS ARE READ ONCE, when a voice starts. A table is an FFT of a
 * quarter of a million points, about fifty milliseconds here, and it
 * happens on the thread that renders -- osc/sampleslot.h's trade for
 * osc/sampleslot.h's reason, and a dropout on the first note of each
 * octave in a live synth -- so a knob swept under a sounding voice must
 * not ask for a new one every window. A sweep changes the next note. At most PAD_TABLES tables are kept per synth; a new one past that
 * replaces the one used longest ago.
 *
 * DETERMINISTIC. The phases come from a fixed seed, drawn by a
 * generator whose sequence is fixed by the standard (std::mt19937) and
 * turned into a phase by hand rather than by a distribution, whose
 * spelling is not. The same params make the same table on every host.
 *
 * `out2' reads the same table 0.38 of its length away, which is a
 * different stretch of the same noise-like wave: the same pad,
 * decorrelated, for a stereo pair from one table.
 *
 * THE TRANSFORM is the radix-2 decimation-in-frequency FFT in
 * plugins/fft/dsp.c, Embree and Kimble's, with its two faults taken out:
 * the twiddle factors are the caller's rather than a function-static
 * keyed on the last size asked for, and computed in double rather than
 * by a float recurrence that has drifted a long way by the end of a
 * quarter-million points; and a failed allocation is the caller's to
 * report, not an exit(1) from inside a library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <atomic>
#include <map>
#include <random>
#include <vector>

#include "think.h"

enum {IN_FREQ, IN_PARTIALS, IN_BANDWIDTH, IN_BWSCALE, IN_TILT, IN_STRETCH,
      OUT_ARG, OUT_ARG2, INOUT_STATE};
int args[INOUT_STATE + 1];

static const char desc[] = "PADsynth (partials with a bandwidth each)";
thPlugin::State    mystate = thPlugin::ACTIVE;

#define PAD_BITS 18
#define PAD_LEN (1u << PAD_BITS)
#define PAD_TABLES 16
#define PAD_PARTIALS_MAX 256
#define PAD_BASE 261.63           /* middle C: octave 0's table */

/* Where `out2' reads, ahead of `out': a prime near 0.38 of the table and
   not half of it. Half would turn every bin's phase by pi times its index,
   which is one sign for the even bins and the other for the odd, and a
   pad with most of its power on one parity comes out correlated with its
   own inverse. */
#define PAD_SECOND 100003u

/* The state: the voice's table, as the params it was built from and the
   octave, then the read position as a whole number of samples and a
   fraction -- two floats, because one float at a quarter of a million
   holds a position to a sixty-fourth of a sample, which is thirteen cents
   of pitch error in the step. */
enum { S_STARTED, S_OCTAVE, S_PARTIALS, S_BANDWIDTH, S_BWSCALE, S_TILT,
       S_STRETCH, S_WHOLE, S_FRAC, S_COUNT };

/* ---- the tables ---------------------------------------------------------
 *
 * Keyed on the plugin, for osc/sampleslot.h's reason: one thPlugin per
 * file per synth, so a slot keyed on it belongs to one synth and one
 * rendering thread. */

struct PadKey
{
    float octave, partials, bandwidth, bwscale, tilt, stretch;

    bool operator< (const PadKey &o) const
    {
        return memcmp(this, &o, sizeof(PadKey)) < 0;
    }
};

struct PadTable
{
    std::vector<float> wave;
    unsigned long used;
};

struct PadSlot
{
    std::atomic<const thPlugin *> owner;
    std::map<PadKey, PadTable> *tables;
    unsigned long clock;
};

#define PAD_SLOTS 64
static PadSlot padSlots[PAD_SLOTS];

static PadSlot *padSlotFor (const thPlugin *plugin)
{
    for (int i = 0; i < PAD_SLOTS; i++)
        if (padSlots[i].owner.load(std::memory_order_acquire) == plugin)
            return &padSlots[i];

    return NULL;
}

/* The FFT. `re' and `im' are 2^m long; forward, in place, natural order
   out. See the head for where it comes from. */
static void padFft (std::vector<double> &re, std::vector<double> &im, int m)
{
    const size_t n = (size_t)1 << m;
    std::vector<double> wr(n / 2), wi(n / 2);

    for (size_t j = 0; j < n / 2; j++)
    {
        wr[j] = cos(M_PI * (double)j / (double)(n / 2));
        wi[j] = -sin(M_PI * (double)j / (double)(n / 2));
    }

    size_t le = n, windex = 1;

    for (int l = 0; l < m; l++)
    {
        const size_t increment = le;

        le >>= 1;

        for (size_t j = 0; j < le; j++)
        {
            const double ur = wr[j * windex], ui = wi[j * windex];

            for (size_t i = j; i < n; i += increment)
            {
                const size_t ip = i + le;
                const double tr = re[i] - re[ip], ti = im[i] - im[ip];

                re[i] += re[ip];
                im[i] += im[ip];
                re[ip] = tr * ur - ti * ui;
                im[ip] = tr * ui + ti * ur;
            }
        }

        windex <<= 1;
    }

    /* Bit-reversed order back to natural, as the original does after. */
    for (size_t i = 1, j = 0; i < n - 1; i++)
    {
        size_t k = n >> 1;

        while (k <= j)
        {
            j -= k;
            k >>= 1;
        }

        j += k;

        if (i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
}

/* One table: see the head. Peak-normalized to 1, since the shape of the
   wave is noise-like and its crest factor depends on the params; a pad
   that clipped at some settings and whispered at others would be worse
   than one whose level moves a few decibels with its bandwidth. */
static bool padBuild (const PadKey &key, unsigned rate,
                      std::vector<float> &wave)
{
    const double base = PAD_BASE * pow(2.0, key.octave);
    /* Half an octave above the base is the fastest this table is read. */
    const double ceiling = rate * 0.49 / sqrt(2.0);
    const double bwRatio = pow(2.0, key.bandwidth / 1200.0) - 1;
    const double binHz = (double)rate / PAD_LEN;
    std::vector<double> mag(PAD_LEN / 2, 0.0);

    for (int n = 1; n <= (int)key.partials; n++)
    {
        const double hz = base * n * sqrt(1 + key.stretch * (double)n * n);

        if (hz >= ceiling)
            break;

        const double amp = pow(10.0, key.tilt * log2((double)n) / 20.0);
        const double width = bwRatio * base * pow((double)n, key.bwscale);
        const double center = hz / binHz;
        const double sigma = width / binHz / 2;

        /* Narrower than a bin is a line: all of it in the nearest one,
           which is what makes `bandwidth = 0' a sum of sines rather than
           a Gaussian sampled at one point and lost between bins. */
        if (sigma < 0.5)
        {
            const size_t k = (size_t)floor(center + 0.5);

            if (k > 0 && k < mag.size())
                mag[k] += amp;

            continue;
        }

        const long lo = (long)floor(center - 4 * sigma);
        const long hi = (long)ceil(center + 4 * sigma);

        /* The Gaussian with its area held to `amp', so a wide band is as
           loud as a narrow one and bandwidth is not a level knob. */
        for (long k = lo < 1 ? 1 : lo; k <= hi && k < (long)mag.size(); k++)
        {
            const double x = ((double)k - center) / sigma;

            mag[(size_t)k] += amp * exp(-0.5 * x * x) /
                              (sigma * sqrt(2 * M_PI));
        }
    }

    std::vector<double> re(PAD_LEN, 0.0), im(PAD_LEN, 0.0);
    std::mt19937 dice(20260922u);

    for (size_t k = 1; k < mag.size(); k++)
    {
        const double phase = 2 * M_PI * (double)dice() / 4294967296.0;

        /* The conjugate of what the inverse wants, so that the forward
           transform below is the inverse: conj(FFT(conj(X))) is N times
           IFFT(X), and the real part is all that is kept. */
        re[k] = mag[k] * cos(phase);
        im[k] = -mag[k] * sin(phase);
        re[PAD_LEN - k] = re[k];
        im[PAD_LEN - k] = -im[k];
    }

    padFft(re, im, PAD_BITS);

    double top = 0;

    for (size_t i = 0; i < PAD_LEN; i++)
        top = fmax(top, fabs(re[i]));

    wave.resize(PAD_LEN);

    for (size_t i = 0; i < PAD_LEN; i++)
        wave[i] = top > 0 ? (float)(re[i] / top) : 0.0f;

    return true;
}

static const std::vector<float> *padTable (const thPlugin *plugin,
                                           const PadKey &key, unsigned rate)
{
    PadSlot *slot = padSlotFor(plugin);

    if (slot == NULL || slot->tables == NULL)
        return NULL;

    std::map<PadKey, PadTable>::iterator i = slot->tables->find(key);

    if (i != slot->tables->end())
    {
        i->second.used = ++slot->clock;
        return &i->second.wave;
    }

    if (slot->tables->size() >= PAD_TABLES)
    {
        std::map<PadKey, PadTable>::iterator oldest = slot->tables->begin();

        for (i = slot->tables->begin(); i != slot->tables->end(); i++)
            if (i->second.used < oldest->second.used)
                oldest = i;

        slot->tables->erase(oldest);
    }

    PadTable &t = (*slot->tables)[key];

    padBuild(key, rate, t.wave);
    t.used = ++slot->clock;

    return &t.wave;
}

void module_cleanup (thPlugin *plugin)
{
    for (int i = 0; i < PAD_SLOTS; i++)
        if (padSlots[i].owner.load(std::memory_order_acquire) == plugin)
        {
            delete padSlots[i].tables;
            padSlots[i].tables = NULL;
            padSlots[i].owner.store(NULL, std::memory_order_release);
            return;
        }
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    for (int i = 0; i < PAD_SLOTS; i++)
    {
        const thPlugin *none = NULL;

        if (padSlots[i].owner.compare_exchange_strong(
                none, plugin, std::memory_order_acq_rel))
        {
            delete padSlots[i].tables;
            padSlots[i].tables = new std::map<PadKey, PadTable>();
            padSlots[i].clock = 0;
            break;
        }
    }

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "The note");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_PARTIALS] = plugin->regArg("partials", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PARTIALS],
                       "How many partials, of those under Nyquist");
    plugin->setArgRange(args[IN_PARTIALS], 1, 128);
    plugin->setArgStep(args[IN_PARTIALS], 1);
    plugin->setArgDefault(args[IN_PARTIALS], 32);
    args[IN_BANDWIDTH] = plugin->regArg("bandwidth", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_BANDWIDTH],
                       "Each partial's width at the fundamental; 0 is a "
                       "sum of sines");
    plugin->setArgUnits(args[IN_BANDWIDTH], "cents");
    plugin->setArgRange(args[IN_BANDWIDTH], 0, 200);
    args[IN_BWSCALE] = plugin->regArg("bwscale", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_BWSCALE],
                       "How the width grows up the series: 1 the same "
                       "cents, 0 the same hertz");
    plugin->setArgRange(args[IN_BWSCALE], 0, 2);
    args[IN_TILT] = plugin->regArg("tilt", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_TILT],
                       "How the partials fall off; -6 is a saw's slope");
    plugin->setArgUnits(args[IN_TILT], "dB per octave");
    plugin->setArgRange(args[IN_TILT], -24, 6);
    args[IN_STRETCH] = plugin->regArg("stretch", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_STRETCH],
                       "Inharmonicity: partial n at n * sqrt(1 + stretch "
                       "n^2)");
    plugin->setArgRange(args[IN_STRETCH], 0, 0.01f);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The pad");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_ARG2] = plugin->regArg("out2", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG2],
                       "The same pad from half the table away, "
                       "decorrelated");
    plugin->setArgRange(args[OUT_ARG2], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG2], "full scale");

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_partials = mod->getArg(node, args[IN_PARTIALS]);
    thArg *in_bandwidth = mod->getArg(node, args[IN_BANDWIDTH]);
    thArg *in_bwscale = mod->getArg(node, args[IN_BWSCALE]);
    thArg *in_tilt = mod->getArg(node, args[IN_TILT]);
    thArg *in_stretch = mod->getArg(node, args[IN_STRETCH]);
    thArg *out_arg = mod->getArg(node, args[OUT_ARG]);
    thArg *out_arg2 = mod->getArg(node, args[OUT_ARG2]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    float *out = out_arg->allocate(windowlen);
    float *out2 = out_arg2->allocate(windowlen);
    float *state = inout_state->allocate(S_COUNT);

    /* The params and the octave, on the voice's first sample. The octave
       is the one whose C is nearest the note, so a table is read at most
       half an octave either side of its base. */
    if (state[S_STARTED] == 0)
    {
        const double freq = thBoundFreq((*in_freq)[0], samples);
        const float partials = (*in_partials)[0];

        state[S_OCTAVE] = (float)thClampArg(
            (float)floor(log2(freq / PAD_BASE) + 0.5), -6, 6);
        state[S_PARTIALS] = partials == 0 ? 32 :
            floorf(thClampArg(partials, 1, PAD_PARTIALS_MAX));
        state[S_BANDWIDTH] = thClampArg((*in_bandwidth)[0], 0, 1200);
        state[S_BWSCALE] = thClampArg((*in_bwscale)[0], 0, 2);
        state[S_TILT] = thClampArg((*in_tilt)[0], -60, 24);
        state[S_STRETCH] = thClampArg((*in_stretch)[0], 0, 0.1f);
        state[S_WHOLE] = 0;
        state[S_FRAC] = 0;
        state[S_STARTED] = 1;
    }

    const PadKey key = { state[S_OCTAVE], state[S_PARTIALS],
                         state[S_BANDWIDTH], state[S_BWSCALE],
                         state[S_TILT], state[S_STRETCH] };
    const std::vector<float> *table = padTable(node->plugin(), key, samples);
    const double base = PAD_BASE * pow(2.0, state[S_OCTAVE]);

    size_t whole = (size_t)state[S_WHOLE];
    /* In float and stepped in float, for delay::chorus's reason: a window
       boundary is not an event. */
    float frac = state[S_FRAC];

    if (whole >= PAD_LEN)
        whole = 0;

    for (unsigned int i = 0; i < windowlen; i++)
    {
        if (table == NULL || table->empty())
        {
            out[i] = out2[i] = 0;
            continue;
        }

        const float *w = &(*table)[0];
        const size_t k2 = (whole + PAD_SECOND) % PAD_LEN;
        const float f = frac;
        const float a = w[whole], b = w[(whole + 1) % PAD_LEN];
        const float a2 = w[k2], b2 = w[(k2 + 1) % PAD_LEN];

        out[i] = TH_MAX * (a + (b - a) * f);
        out2[i] = TH_MAX * (a2 + (b2 - a2) * f);

        /* The step split the same way: whole samples to the index and the
           rest to the fraction, which carries into it. */
        const double step = thBoundFreq((*in_freq)[i], samples) / base;
        const double stepWhole = floor(step);

        frac += (float)(step - stepWhole);
        whole += (size_t)stepWhole;

        if (frac >= 1)
        {
            frac -= 1;
            whole += 1;
        }

        whole %= PAD_LEN;
    }

    state[S_WHOLE] = (float)whole;
    state[S_FRAC] = frac;

    return 0;
}
