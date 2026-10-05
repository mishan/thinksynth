/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

/* A voice that speaks: a formant synthesizer in the Klatt style.
 *
 * A glottal pulse at `freq', and noise, through resonators whose
 * frequencies, bandwidths and gains come from a table of phonemes and
 * glide from one phoneme's to the next. What it says is `say', a run of
 * ARPAbet codes (libthink/thPhoneme.h) that a note brings with it --
 * wired from `ionode->say' -- and with nothing there it sings AA.
 *
 * TWO PATHS, as in Klatt's 1980 synthesizer. The voiced sound goes
 * through five resonators in CASCADE, F1 to F5, each with unity gain at
 * DC, so the formants' relative levels come out of the frequencies the
 * way they do in a throat and nobody has to set them. The noise of a
 * fricative or a burst goes through resonators in PARALLEL, F2 to F6
 * with a gain each, plus a bypass for the flat ones (F, TH), because a
 * fricative's spectrum is a place in the mouth and not a vowel's shape.
 * Aspiration, the breath of HH and of a P before its vowel, joins the
 * voicing at the top of the cascade.
 *
 * IT SINGS RATHER THAN READS. The first vowel of what a note says is the
 * syllable's nucleus, and it is held for as long as the note is: the
 * consonants before it are spoken at their own lengths, the vowel lasts
 * until the key comes up, and the consonants after it are spoken then.
 * `come' held for a bar is K, a bar of AH, and the M at the release.
 * A note let go before its vowel is reached says the vowel at its own
 * length rather than skipping it. `play' stays up until the last
 * phoneme has been said, so the graph's envelope can follow the words
 * rather than the key.
 *
 * A STOP IS THREE SOUNDS: a closure (silence, or a low voice bar for B,
 * D and G), a burst of noise shaped by where the tongue was, and for P,
 * T and K a breath of aspiration on the next vowel's formants before the
 * voicing starts. The formants head for the stop's locus during the
 * closure and leave it with the burst, and that movement is most of how
 * a B is told from a D.
 *
 * A MONO SLIDE onto the next syllable restarts the words without
 * restarting anything else: the resonators and the glide carry, so the
 * line is sung legato.
 *
 * `shift' scales every formant: 1 is an adult male, about 1.15 an adult
 * female, and under 0.8 a giant. `buzz' moves the source from a glottal
 * pulse toward a raw sawtooth, which is the robot.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"
#include "thPhoneme.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

enum {IN_FREQ, IN_SAY, IN_TRIGGER, IN_RATE, IN_SHIFT, IN_BUZZ, IN_BREATH,
      OUT_ARG, OUT_PLAY, INOUT_STATE};

std::atomic<int> args[INOUT_STATE + 1];

static const char desc[] = "Speech (formant synthesizer, ARPAbet)";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* ---- the phonemes -------------------------------------------------------- */

/* What a phoneme is made of, as targets the glide heads for. */
enum Kind { VOWEL, DIPHTHONG, SONORANT, FRICATIVE, ASPIRATE, STOP, AFFRICATE,
            PAUSE };

/* Where a stop's burst is: the noise spectrum it releases with. */
enum Place { LABIAL, ALVEOLAR, VELAR };

struct Phoneme
{
    const char *name;
    Kind  kind;
    float dur;              /* ms */
    float f1, f2, f3;       /* Hz; a stop's locus */
    float b1, b2, b3;       /* Hz */
    float av;               /* voicing, 0 to 1 */
    float af;               /* frication into the parallel path */
    float a2, a3, a4, a5, a6, ab;   /* its gains, and the bypass */
    float g1, g2, g3;       /* a diphthong's second target */
    int   voiced;           /* a stop or an affricate: B D G JH */
    Place place;            /* a stop's burst */
    int   fric;             /* an affricate's fricative, by code */
};

/* Adult male targets. The vowels follow Peterson and Barney's averages and
   Klatt's 1980 table, the consonants Klatt's and Holmes's; the gains are
   this synthesizer's, set by ear and by statecheck's spectra. */
static const Phoneme table[] = {
    /* name kind      dur   f1   f2    f3   b1  b2   b3  av  af */
    { "AA", VOWEL,     170, 700, 1220, 2600, 130, 70, 160, 1, 0 },
    { "AE", VOWEL,     180, 620, 1660, 2430, 70, 130, 300, 1, 0 },
    { "AH", VOWEL,     110, 620, 1220, 2550, 80, 50, 140, 1, 0 },
    { "AO", VOWEL,     170, 600, 990, 2570, 90, 100, 80, 1, 0 },
    { "AW", DIPHTHONG, 230, 640, 1230, 2550, 80, 70, 140, 1, 0,
      0, 0, 0, 0, 0, 0, 420, 940, 2350 },
    { "AY", DIPHTHONG, 230, 660, 1200, 2550, 100, 70, 200, 1, 0,
      0, 0, 0, 0, 0, 0, 400, 1880, 2500 },
    { "EH", VOWEL,     120, 530, 1680, 2500, 60, 90, 200, 1, 0 },
    { "ER", VOWEL,     160, 470, 1270, 1540, 100, 60, 110, 1, 0 },
    { "EY", DIPHTHONG, 190, 480, 1720, 2520, 70, 100, 200, 1, 0,
      0, 0, 0, 0, 0, 0, 330, 2020, 2600 },
    { "IH", VOWEL,     110, 400, 1800, 2570, 50, 100, 140, 1, 0 },
    { "IY", VOWEL,     160, 310, 2020, 2960, 45, 100, 200, 1, 0 },
    { "OW", DIPHTHONG, 180, 540, 1100, 2300, 80, 70, 70, 1, 0,
      0, 0, 0, 0, 0, 0, 450, 900, 2300 },
    { "OY", DIPHTHONG, 250, 550, 960, 2400, 80, 50, 130, 1, 0,
      0, 0, 0, 0, 0, 0, 360, 1820, 2450 },
    { "UH", VOWEL,     110, 450, 1100, 2350, 80, 100, 80, 1, 0 },
    { "UW", VOWEL,     170, 300, 900, 2250, 65, 110, 140, 1, 0 },

    /* name kind      dur   f1   f2    f3   b1  b2   b3  av  af
                       a2 a3 a4 a5 a6 ab  g1 g2 g3  voiced place fric */
    { "B",  STOP,       60, 200, 1100, 2150, 60, 110, 130, 0.25f, 0.4f,
      0, 0, 0, 0, 0, 0.2f, 0, 0, 0, 1, LABIAL, 0 },
    { "CH", AFFRICATE, 140, 350, 1800, 2820, 200, 90, 300, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ALVEOLAR, 32 },
    { "D",  STOP,       50, 200, 1600, 2600, 60, 100, 170, 0.25f, 0.4f,
      0, 0, 0, 0.5f, 0.7f, 0, 0, 0, 0, 1, ALVEOLAR, 0 },
    { "DH", FRICATIVE,  50, 270, 1290, 2540, 60, 80, 170, 0.5f, 0.3f,
      0, 0, 0, 0.2f, 0.3f, 0.1f },
    { "F",  FRICATIVE, 100, 340, 1100, 2080, 200, 120, 150, 0, 0.6f,
      0, 0, 0, 0, 0.2f, 0.15f },
    { "G",  STOP,       50, 200, 1990, 2850, 60, 150, 280, 0.25f, 0.5f,
      0, 0.8f, 0.6f, 0, 0, 0, 0, 0, 0, 1, VELAR, 0 },
    { "HH", ASPIRATE,   60, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "JH", AFFRICATE, 100, 260, 1800, 2820, 60, 80, 270, 0.4f, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 1, ALVEOLAR, 39 },
    { "K",  STOP,       60, 300, 1990, 2850, 250, 160, 330, 0, 0.6f,
      0, 0.8f, 0.6f, 0.2f, 0, 0, 0, 0, 0, 0, VELAR, 0 },
    { "L",  SONORANT,   70, 310, 1050, 2880, 50, 100, 280, 0.8f, 0 },
    { "M",  SONORANT,   70, 250, 1100, 2150, 100, 200, 300, 0.6f, 0 },
    { "N",  SONORANT,   70, 250, 1700, 2600, 100, 200, 300, 0.6f, 0 },
    { "NG", SONORANT,   70, 250, 2300, 2750, 100, 200, 300, 0.6f, 0 },
    { "P",  STOP,       70, 400, 1100, 2150, 300, 150, 220, 0, 0.6f,
      0, 0, 0, 0, 0, 0.25f, 0, 0, 0, 0, LABIAL, 0 },
    { "R",  SONORANT,   70, 310, 1060, 1380, 70, 100, 120, 0.8f, 0 },
    { "S",  FRICATIVE, 120, 320, 1390, 2530, 200, 80, 200, 0, 0.8f,
      0, 0, 0, 0.2f, 1, 0 },
    { "SH", FRICATIVE, 120, 300, 1840, 2750, 200, 100, 300, 0, 0.8f,
      0, 1, 0.6f, 0.2f, 0, 0 },
    { "T",  STOP,       60, 400, 1600, 2600, 300, 120, 250, 0, 0.6f,
      0, 0, 0, 0.6f, 0.8f, 0, 0, 0, 0, 0, ALVEOLAR, 0 },
    { "TH", FRICATIVE, 100, 320, 1290, 2540, 200, 90, 200, 0, 0.5f,
      0, 0, 0, 0.2f, 0.3f, 0.1f },
    { "V",  FRICATIVE,  70, 220, 1100, 2080, 60, 90, 120, 0.5f, 0.4f,
      0, 0, 0, 0, 0.2f, 0.12f },
    { "W",  SONORANT,   60, 290, 610, 2150, 50, 80, 60, 0.8f, 0 },
    { "Y",  SONORANT,   60, 260, 2070, 3020, 40, 250, 500, 0.8f, 0 },
    { "Z",  FRICATIVE,  80, 240, 1390, 2530, 70, 60, 180, 0.5f, 0.6f,
      0, 0, 0, 0.2f, 1, 0 },
    { "ZH", FRICATIVE,  80, 300, 1840, 2750, 60, 90, 200, 0.5f, 0.6f,
      0, 1, 0.6f, 0.2f, 0, 0 },
    { "_",  PAUSE,      80, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static_assert(sizeof(table) / sizeof(table[0]) == TH_PHONEMES,
              "one row per phoneme in thPhoneme.h");

/* ---- what the glide heads for -------------------------------------------- */

/* Everything that moves, in the order it is kept in the state. */
enum { P_F1, P_F2, P_F3, P_B1, P_B2, P_B3, P_AV, P_AH, P_AF, P_A2, P_A3, P_A4,
       P_A5, P_A6, P_AB, P_COUNT };

/* The formants and bandwidths glide slowly, and the gains fast: a vowel
   bends into the next over tens of milliseconds, but a stop's burst is
   over in ten. */
#define FORMANT_GLIDE_MS 12.0f
#define GAIN_GLIDE_MS     3.0f

/* A segment: one target held for a time. A phoneme is one or more. */
struct Segment
{
    float target[P_COUNT];
    float dur;              /* samples */
    bool  hold;             /* the nucleus: held while the key is down */
};

#define MAX_SEGMENTS (3 * TH_NOTE_SAY + 1)

static const float silent[3] = { 500, 1500, 2500 };

static void fromRow (const Phoneme &p, float *t)
{
    t[P_F1] = p.f1;  t[P_F2] = p.f2;  t[P_F3] = p.f3;
    t[P_B1] = p.b1;  t[P_B2] = p.b2;  t[P_B3] = p.b3;
    t[P_AV] = p.av;  t[P_AH] = 0;     t[P_AF] = p.af;
    t[P_A2] = p.a2;  t[P_A3] = p.a3;  t[P_A4] = p.a4;
    t[P_A5] = p.a5;  t[P_A6] = p.a6;  t[P_AB] = p.ab;
}

/* The formants of the next phoneme that has some, for an aspiration
   or an HH, which are breath already shaped like what follows. */
static void nextFormants (const int *codes, int n, int from, float *t)
{
    float f[3] = { silent[0], silent[1], silent[2] };
    float b[3] = { 80, 90, 150 };

    for (int i = from; i < n; i++)
    {
        const Phoneme &p = table[codes[i] - 1];

        if (p.kind == VOWEL || p.kind == DIPHTHONG || p.kind == SONORANT)
        {
            f[0] = p.f1;  f[1] = p.f2;  f[2] = p.f3;
            b[0] = p.b1;  b[1] = p.b2;  b[2] = p.b3;
            break;
        }
    }

    t[P_F1] = f[0];  t[P_F2] = f[1];  t[P_F3] = f[2];
    t[P_B1] = b[0] * 2;  t[P_B2] = b[1] * 1.5f;  t[P_B3] = b[2] * 1.5f;
}

static void silence (float *t)
{
    memset(t, 0, P_COUNT * sizeof(float));
    t[P_F1] = silent[0];  t[P_F2] = silent[1];  t[P_F3] = silent[2];
    t[P_B1] = 100;  t[P_B2] = 100;  t[P_B3] = 150;
}

/* What `codes' says, as segments. The first vowel is the one held. */
static int segments (const int *codes, int n, float ms, Segment *out)
{
    static const int sing[] = { 1 };     /* AA, for a note that says nothing */
    int count = 0;
    bool nucleus = false;

    if (n == 0)
    {
        codes = sing;
        n = 1;
    }

    for (int i = 0; i < n; i++)
    {
        const Phoneme &p = table[codes[i] - 1];
        Segment *s = &out[count];
        const bool vowel = p.kind == VOWEL || p.kind == DIPHTHONG;
        const bool hold = vowel && !nucleus;

        if (vowel)
            nucleus = true;

        switch (p.kind)
        {
            case VOWEL:
            case SONORANT:
            case FRICATIVE:
                fromRow(p, s->target);
                s->dur = p.dur * ms;
                s->hold = hold;
                count++;
                break;

            case DIPHTHONG:
                fromRow(p, s->target);
                s->dur = p.dur * 0.55f * ms;
                s->hold = hold;
                s[1] = s[0];
                s[1].target[P_F1] = p.g1;
                s[1].target[P_F2] = p.g2;
                s[1].target[P_F3] = p.g3;
                s[1].dur = p.dur * 0.45f * ms;
                s[1].hold = false;
                count += 2;
                break;

            case ASPIRATE:
                silence(s->target);
                nextFormants(codes, n, i + 1, s->target);
                s->target[P_AH] = 0.6f;
                s->dur = p.dur * ms;
                s->hold = false;
                count++;
                break;

            case STOP:
            case AFFRICATE:
            {
                /* The closure: heading for the locus, silent but for the
                   voice bar. */
                silence(s->target);
                s->target[P_F2] = p.f2;
                s->target[P_F3] = p.f3;
                s->target[P_AV] = p.voiced ? p.av : 0;
                s->target[P_F1] = p.voiced ? 200 : p.f1;
                s->dur = p.dur * ms;
                s->hold = false;
                count++;

                /* The burst, or an affricate's fricative. */
                Segment *b = &out[count];

                if (p.kind == AFFRICATE)
                {
                    fromRow(table[p.fric - 1], b->target);
                    b->target[P_AV] = p.voiced ? p.av : 0;
                    b->dur = table[p.fric - 1].dur * 0.8f * ms;
                }
                else
                {
                    fromRow(p, b->target);
                    b->target[P_AV] = p.voiced ? 0.3f : 0;
                    b->dur = (p.place == VELAR ? 15 : 10) * ms;
                }
                b->hold = false;
                count++;

                /* And for P, T and K the breath before the voicing. */
                if (p.kind == STOP && !p.voiced)
                {
                    Segment *a = &out[count];

                    silence(a->target);
                    nextFormants(codes, n, i + 1, a->target);
                    a->target[P_AH] = 0.4f;
                    a->dur = 50 * ms;
                    a->hold = false;
                    count++;
                }
                break;
            }

            case PAUSE:
                silence(s->target);
                s->dur = p.dur * ms;
                s->hold = false;
                count++;
                break;
        }
    }

    return count;
}

/* ---- the resonators ------------------------------------------------------ */

/* Klatt's two-pole resonator, unity gain at DC. `s' is its two samples of
   history. */
static inline float resonate (float x, float f, float bw, float rate,
                              float *s)
{
    const float r = expf(-(float)M_PI * bw / rate);
    const float c = -r * r;
    const float b = 2 * r * cosf(2 * (float)M_PI * f / rate);
    const float a = 1 - b - c;
    const float y = a * x + b * s[0] + c * s[1];

    s[1] = s[0];
    s[0] = y;

    return y;
}

/* The fixed formants above F3, and the parallel path's top two. */
#define F4 3300.0f
#define F5 3850.0f
#define F6 6000.0f

/* ---- the state ----------------------------------------------------------- */

/* Laid out in one ARG_STATE, floats throughout so that a window boundary is
   not an event. */
enum {
    S_STARTED,
    S_SEGMENT,              /* which segment is being said */
    S_INTO,                 /* samples into it */
    S_PHASE,                /* the glottal cycle, 0 to 1 */
    S_SEEDHI, S_SEEDLO,     /* the noise, sixteen bits each */
    S_TAIL,                 /* samples since the words ended */
    S_GLIDE,                /* P_COUNT: where the glide is */
    S_CASCADE = S_GLIDE + P_COUNT,      /* five resonators, two each */
    S_PARALLEL = S_CASCADE + 10,        /* five */
    S_FLOW = S_PARALLEL + 10,           /* the last glottal flow sample */
    S_HUSH,                 /* the noise's low-pass, two samples */
    S_HUSH2,
    S_SAID,                 /* TH_NOTE_SAY: what was being said */
    S_LEN = S_SAID + TH_NOTE_SAY
};

/* How long `play' stays up after the last phoneme, so the glide can
   bring the gains down rather than the envelope cutting them. */
#define TAIL_MS 40.0f

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    for (int k = 0; k < TH_PHONEMES; k++)
        if (strcmp(table[k].name, thPhonemeNames[k]) != 0)
            return 1;

    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_FREQ] = plugin->regArg("freq", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_FREQ], "The pitch of the voice");
    plugin->setArgUnits(args[IN_FREQ], "Hz");
    args[IN_SAY] = plugin->regArg("say", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SAY],
                       "What to say: phoneme codes ending in 0, as "
                       "`ionode->say' carries them. None sings AA");
    args[IN_TRIGGER] = plugin->regArg("trigger", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_TRIGGER],
                       "Above 0 the key is down and the vowel is held; at 0 "
                       "the rest of the syllable is said");
    plugin->setArgRange(args[IN_TRIGGER], 0, 2);
    args[IN_RATE] = plugin->regArg("rate", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RATE],
                       "How fast the phonemes go by; 1 is a natural pace");
    plugin->setArgRange(args[IN_RATE], 0.25f, 4);
    plugin->setArgDefault(args[IN_RATE], 1);
    plugin->setArgUnits(args[IN_RATE], "ratio");
    args[IN_SHIFT] = plugin->regArg("shift", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_SHIFT],
                       "Every formant times this: 1 an adult male, 1.15 an "
                       "adult female, under 0.8 a giant");
    plugin->setArgRange(args[IN_SHIFT], 0.5f, 2);
    plugin->setArgDefault(args[IN_SHIFT], 1);
    plugin->setArgUnits(args[IN_SHIFT], "ratio");
    args[IN_BUZZ] = plugin->regArg("buzz", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_BUZZ],
                       "The source: 0 a glottal pulse, 1 a raw sawtooth");
    plugin->setArgRange(args[IN_BUZZ], 0, 1);
    args[IN_BREATH] = plugin->regArg("breath", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_BREATH],
                       "Aspiration under the voicing, for a whisper");
    plugin->setArgRange(args[IN_BREATH], 0, 1);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "The voice");
    plugin->setArgRange(args[OUT_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_PLAY] = plugin->regArg("play", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_PLAY],
                       "1 until the last phoneme has been said");
    plugin->setArgRange(args[OUT_PLAY], 0, 1);

    args[INOUT_STATE] = plugin->regArg("state", thPlugin::ARG_STATE);

    return 0;
}

/* The codes in `say', up to its first 0 or anything that is not one. */
static int readSay (thArg *say, int *codes)
{
    int n = 0;
    const unsigned len = say ? say->len() : 0;

    for (unsigned i = 0; i < len && n < TH_NOTE_SAY - 1; i++)
    {
        const float v = (*say)[i];

        if (!(v >= 1 && v <= TH_PHONEMES))
            break;

        codes[n++] = (int)v;
    }

    return n;
}

/* A uniform -1..1 from a 32-bit LCG kept as two exact 16-bit floats. */
static inline float noise (float *hi, float *lo)
{
    unsigned int s = ((unsigned int)*hi << 16) | (unsigned int)*lo;

    s = s * 1664525u + 1013904223u;
    *hi = (float)(s >> 16);
    *lo = (float)(s & 0xffff);

    return (float)(s >> 8) / 8388608.0f - 1;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_freq = mod->getArg(node, args[IN_FREQ]);
    thArg *in_say = mod->getArg(node, args[IN_SAY]);
    thArg *in_trigger = mod->getArg(node, args[IN_TRIGGER]);
    thArg *in_rate = mod->getArg(node, args[IN_RATE]);
    thArg *in_shift = mod->getArg(node, args[IN_SHIFT]);
    thArg *in_buzz = mod->getArg(node, args[IN_BUZZ]);
    thArg *in_breath = mod->getArg(node, args[IN_BREATH]);
    thArg *inout_state = mod->getArg(node, args[INOUT_STATE]);

    float st[S_LEN];
    const unsigned had = inout_state->len();

    for (int k = 0; k < S_LEN; k++)
        st[k] = (unsigned)k < had ? (*inout_state)[k] : 0;

    float *state = inout_state->allocate(S_LEN);
    float *out = mod->getArg(node, args[OUT_ARG])->allocate(windowlen);
    float *play = mod->getArg(node, args[OUT_PLAY])->allocate(windowlen);

    const float rate = (float)samples;
    int codes[TH_NOTE_SAY];
    const int n = readSay(in_say, codes);

    /* A new utterance: the first window of a voice, or a slide onto
       another note. `say' as a whole, its stamp included (SAYARG), so a
       slide onto the same words is one too. */
    float said[TH_NOTE_SAY] = {};

    for (unsigned k = 0; in_say && k < in_say->len() && k < TH_NOTE_SAY; k++)
        said[k] = (*in_say)[k];

    bool fresh = st[S_STARTED] == 0;

    for (int k = 0; !fresh && k < TH_NOTE_SAY; k++)
        fresh = st[S_SAID + k] != said[k];

    if (fresh)
    {
        if (st[S_STARTED] == 0)
        {
            float t[P_COUNT];

            silence(t);
            memcpy(st + S_GLIDE, t, sizeof(t));
            st[S_SEEDLO] = 1;
        }

        st[S_STARTED] = 1;
        st[S_SEGMENT] = 0;
        st[S_INTO] = 0;
        st[S_TAIL] = 0;

        memcpy(st + S_SAID, said, sizeof(said));
    }

    const float formantK = 1 - expf(-1000 / (FORMANT_GLIDE_MS * rate));
    const float gainK = 1 - expf(-1000 / (GAIN_GLIDE_MS * rate));
    const float hushK = 1 - expf(-2 * (float)M_PI * 9000 / rate);

    /* Once a window: `rate' moves the words' pace a window late at worst. */
    Segment segs[MAX_SEGMENTS];
    const int count = segments(codes, n,
                               rate / 1000 / thClampArg((*in_rate)[0],
                                                        0.25f, 4), segs);

    for (unsigned int i = 0; i < windowlen; i++)
    {
        const bool held = (*in_trigger)[i] > 0;
        int seg = (int)st[S_SEGMENT];
        float target[P_COUNT];

        /* Where the words are. A held nucleus waits for the key; anything
           else moves on when its time is up. */
        if (seg < count)
        {
            const Segment &s = segs[seg];

            if (st[S_INTO] >= s.dur && !(s.hold && held))
            {
                seg++;
                st[S_SEGMENT] = (float)seg;
                st[S_INTO] = 0;
            }
        }

        if (seg < count)
        {
            memcpy(target, segs[seg].target, sizeof(target));
            st[S_INTO] += 1;
        }
        else
        {
            silence(target);
            st[S_TAIL] += 1;
        }

        /* Landed once within a millionth: a gain gliding to 0 would
           otherwise sink into denormals and stay there, which costs a
           voice half as much again for the rest of its vowel. */
        for (int k = 0; k < P_COUNT; k++)
        {
            const float g = k <= P_B3 ? formantK : gainK;
            const float d = target[k] - st[S_GLIDE + k];

            if (fabsf(d) < 1e-6f)
                st[S_GLIDE + k] = target[k];
            else
                st[S_GLIDE + k] += g * d;
        }

        const float *p = st + S_GLIDE;
        const float shift = thClampArg((*in_shift)[i], 0.5f, 2);
        const float buzz = thClampArg((*in_buzz)[i], 0, 1);
        const float breath = thClampArg((*in_breath)[i], 0, 1);
        const float f0 = (float)thBoundFreq((double)(*in_freq)[i], samples);
        const float nyq = rate * 0.45f;

        /* The source. Rosenberg's glottal flow, rising for 40% of the
           cycle, falling for 10% and closed for the rest; its first
           difference is what the lips radiate. The
           sawtooth is that with the rounding taken off. */
        float phase = st[S_PHASE] + f0 / rate;

        phase -= floorf(phase);
        st[S_PHASE] = phase;

        const float open = 0.4f, close = 0.1f;
        float flow;

        if (phase < open)
            flow = 0.5f * (1 - cosf((float)M_PI * phase / open));
        else if (phase < open + close)
            flow = cosf((float)M_PI * 0.5f * (phase - open) / close);
        else
            flow = 0;

        const float pulse = (flow - st[S_FLOW]) * rate / (f0 > 1 ? f0 : 1) /
                            12;
        const float saw = 1 - 2 * phase;
        const float voice = pulse + (saw * 0.5f - pulse) * buzz;

        st[S_FLOW] = flow;

        /* Klatt's synthesizer ran at 10 kHz, and its noise stopped at 5;
           white to 22 kHz is a hiss over every fricative. Two poles at
           9 kHz. */
        st[S_HUSH] += hushK * (noise(&st[S_SEEDHI], &st[S_SEEDLO]) -
                               st[S_HUSH]);
        st[S_HUSH2] += hushK * (st[S_HUSH] - st[S_HUSH2]);

        const float hiss = st[S_HUSH2] * 1.6f;
        const float asp = (p[P_AH] + breath * 0.3f) * hiss * 0.5f;
        const float voicing = p[P_AV] * voice;

        /* The cascade: voicing and aspiration through F1 to F5. */
        float y = voicing + asp;

        y = resonate(y, fminf(p[P_F1] * shift, nyq), p[P_B1], rate,
                     st + S_CASCADE);
        y = resonate(y, fminf(p[P_F2] * shift, nyq), p[P_B2], rate,
                     st + S_CASCADE + 2);
        y = resonate(y, fminf(p[P_F3] * shift, nyq), p[P_B3], rate,
                     st + S_CASCADE + 4);
        y = resonate(y, fminf(F4 * shift, nyq), 250, rate,
                     st + S_CASCADE + 6);
        y = resonate(y, fminf(F5 * shift, nyq), 300, rate,
                     st + S_CASCADE + 8);

        /* The parallel path: frication through F2 to F6 and past them.
           Each resonator is unity at DC, which is about Q at its peak, so
           its output is scaled down by Q to put its amplitude there. */
        const float fric = p[P_AF] * hiss;
        const float pf[5] = { p[P_F2], p[P_F3], F4, F5, F6 };
        const float pb[5] = { 200, 250, 300, 400, 800 };
        const float pa[5] = { p[P_A2], p[P_A3], p[P_A4], p[P_A5], p[P_A6] };
        float par = fric * p[P_AB];

        for (int r = 0; r < 5; r++)
        {
            const float f = fminf(pf[r] * shift, nyq);

            if (pa[r] > 0.001f)
                par += pa[r] * resonate(fric, f, pb[r], rate,
                                        st + S_PARALLEL + 2 * r) *
                       pb[r] / f;
            else
                resonate(0, f, pb[r], rate, st + S_PARALLEL + 2 * r);
        }

        float v = y * 0.25f + par * 0.5f;

        if (!thIsFinite(v))
        {
            v = 0;
            memset(st + S_CASCADE, 0, 20 * sizeof(float));
        }

        out[i] = thClampArg(v, -2, 2) * TH_MAX;

        const bool speaking = seg < count || st[S_TAIL] < TAIL_MS * rate / 1000;

        play[i] = speaking ? 1 : 0;
    }

    memcpy(state, st, sizeof(st));

    return 0;
}
