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

#ifndef THINK_SAMPLESLOT_H
#define THINK_SAMPLESLOT_H

/* The wavs osc::sample plays, read once and shared by every voice.
 *
 * ONE TABLE PER SYNTH, keyed on the thPlugin the synth's plugin manager
 * loaded -- osc/noiseslot.h's arrangement, for osc/noiseslot.h's reason.
 * Every synth loads its own thPlugin for a given file and every node
 * built from it reaches that pointer through node->plugin(), so a slot
 * claimed in module_init and keyed on it is a table that one synth owns,
 * on the one thread that synth renders on. Sixteen voices of a drum
 * graph share one copy of the drum; two synths do not share anything,
 * which is what keeps a render reproducible.
 *
 * WHERE THE READ HAPPENS, and why it is not at load. A missing .dsp node
 * fails the file at parse, and a missing wav ought to as well -- but
 * nothing between the grammar and the graph knows that a quoted name is
 * a filename. thArg::ARG_TEXT says "this is a name", not "this is a
 * file"; only the plugin knows that, and the plugin's first chance to
 * say anything is its first callback. Teaching the engine would mean a
 * field on thPlugin::ArgInfo, which changes the stride of a vector whose
 * accessors are inlined into every host -- a soname bump, which is a
 * bigger thing than this. So the read is on the first window that asks
 * for it, cached either way, and a failure is one line on stderr naming
 * the file and then silence with `play' at 0, which ends the note rather
 * than hanging it.
 *
 * WHAT THAT COSTS, said plainly because the paragraph above is an
 * argument about when and not about how much. The first window to name a
 * file does a fopen, a whole decode, an O(n) resample and a map insert,
 * all on the thread that is supposed to be handing back a buffer -- so
 * the first note on a long sample is a dropout in the live synth, and
 * free under genwav, which has no deadline. It is a one-off per file per
 * synth and the kit this tree ships is six short drums, which is why it
 * is worth paying rather than worth a soname bump. If it ever stops
 * being worth paying, the fix is a pass over the graph at load that asks
 * each osc::sample node for its `file' and primes the table off the
 * audio thread; nothing here would have to change for that to work.
 *
 * A FAILURE IS CACHED TOO. Without that, a graph naming a file that is
 * not there would stat() once per window per voice forever and print a
 * line each time. The entry is remembered with no frames in it, which is
 * the same thing the reader returns for an empty file, and both play as
 * silence.
 *
 * MONO, AT THE SYNTH'S RATE. A stereo file is summed -- a voice has one
 * output and a graph that wants two instantiates two nodes, the way
 * fx/chorus.dsp does -- and a file recorded at another rate is resampled
 * by ratio here, once, rather than by every voice on every window. The
 * resampling is linear, which is the same interpolation the playback
 * does and no worse than it.
 */

#include <atomic>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "thUtil.h"

#define THINK_SAMPLE_SLOTS 64

/* One wav, mono, at the synth's rate. `frames' empty is a file that
   would not read: see the head. */
struct thSampleData
{
    std::vector<float> frames;
};

struct thSampleSlot
{
    std::atomic<const thPlugin *>        owner;
    std::map<std::string, thSampleData> *table;  /* owner's thread only */
    /* The next velocity layer for each primary file. All voices in a synth
       reach the same slot, so alternate hits do not restart at layer one. */
    std::map<std::string, unsigned char> alternates;
};

static thSampleSlot thSampleSlots[THINK_SAMPLE_SLOTS];

static inline thSampleSlot *thSampleSlotFor (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
        if (thSampleSlots[i].owner.load(std::memory_order_acquire) == plugin)
            return &thSampleSlots[i];

    return NULL;
}

static inline void thSampleClaim (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
    {
        const thPlugin *none = NULL;

        if (thSampleSlots[i].owner.compare_exchange_strong(
                none, plugin, std::memory_order_acq_rel))
        {
            delete thSampleSlots[i].table;
            thSampleSlots[i].table = new std::map<std::string, thSampleData>();
            thSampleSlots[i].alternates.clear();
            return;
        }
    }
}

static inline void thSampleRelease (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
        if (thSampleSlots[i].owner.load(std::memory_order_acquire) == plugin)
        {
            delete thSampleSlots[i].table;
            thSampleSlots[i].table = NULL;
            thSampleSlots[i].alternates.clear();
            thSampleSlots[i].owner.store(NULL, std::memory_order_release);
            return;
        }
}

/* Count a trigger once across every voice using this primary file. The slot
   belongs to one synth and is cleared when that synth releases its plugin. */
static inline unsigned thSampleNextLayer (const thPlugin *plugin,
                                           const std::string &name)
{
    thSampleSlot *slot = thSampleSlotFor(plugin);

    if (slot == NULL)
        return 0;

    unsigned char &next = slot->alternates[name];
    const unsigned chosen = next;

    next = (unsigned char)((next + 1) % 3);

    return chosen;
}

/* ---- the reader ---------------------------------------------------------
 *
 * Sixty-odd lines of RIFF walk rather than a library, because what is
 * needed is two formats and a chunk loop, and a dependency that has to
 * be present on four platforms and in an emscripten build is a bigger
 * commitment than that.
 *
 * Little-endian by construction: the bytes are assembled with shifts
 * rather than read over a struct, so a big-endian host reads the same
 * file the same way. (thinksynth has a TestBigEndian in its CMake and a
 * struct read here would be the one place that quietly stopped being
 * true.)
 */

static inline unsigned thWavU16 (const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static inline unsigned long thWavU32 (const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* The most frames this reader will hand back, which is the most osc::sample
 * can address: the playhead is a float, and past 2^24 a float can no longer
 * hold a frame index and its fraction. About six minutes at 44.1 kHz. It is
 * TH_WAVELENGTH_MAX for that reason and not by coincidence -- thBoundFreq's
 * floor is the same limit on the same arithmetic.
 *
 * It is a refusal rather than a truncation because the two things that reach
 * it are a genuinely long file, where playing the first six minutes and
 * saying nothing would be the wrong answer, and a header whose sample rate
 * is not a sample rate, where a 244-byte file claiming 1 Hz asks for a
 * hundred seconds of audio. Both want to be told about.
 */
#define THINK_SAMPLE_FRAMES_MAX ((size_t)TH_WAVELENGTH_MAX)

/* Reads `path' into `out', mono, resampled to `rate'. False and an empty
   `out' on anything it does not understand; `why' says which. */
static inline bool thWavRead (const std::string &path, unsigned rate,
                              thSampleData &out, std::string &why)
{
    FILE *f = fopen(path.c_str(), "rb");

    out.frames.clear();

    if (f == NULL)
    {
        why = "could not be opened";
        return false;
    }

    /* How many bytes there actually are, so that a `data' chunk header
       claiming more than the file holds cannot size an allocation. A
       truncated or corrupt wav is a thing that happens; reserving the
       4 GB its length field asks for is not a thing that should. */
    long fileBytes = 0;

    if (fseek(f, 0, SEEK_END) == 0)
        fileBytes = ftell(f);

    if (fileBytes < 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        why = "could not be measured";
        return false;
    }

    unsigned char head[12];

    if (fread(head, 1, 12, f) != 12 ||
        memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0)
    {
        fclose(f);
        why = "is not a RIFF/WAVE file";
        return false;
    }

    unsigned channels = 0, bits = 0, format = 0;
    unsigned long fileRate = 0;
    bool haveFmt = false;

    for (;;)
    {
        unsigned char ch[8];

        if (fread(ch, 1, 8, f) != 8)
            break;                              /* end of the chunks */

        const unsigned long len = thWavU32(ch + 4);

        if (memcmp(ch, "fmt ", 4) == 0 && len >= 16)
        {
            unsigned char fmt[40];
            const unsigned long want = (len > sizeof(fmt)) ? sizeof(fmt) : len;

            if (fread(fmt, 1, want, f) != want)
                break;

            format = thWavU16(fmt);
            channels = thWavU16(fmt + 2);
            fileRate = thWavU32(fmt + 4);
            bits = thWavU16(fmt + 14);

            /* WAVE_FORMAT_EXTENSIBLE says nothing itself; the two bytes
               at the front of its GUID are the format it stands for, and
               every 24-bit and multichannel file a modern tool writes is
               one of these. */
            if (format == 0xfffe && want >= 26)
                format = thWavU16(fmt + 24);

            haveFmt = true;

            /* Chunks are padded to an even length, and a `fmt ' of 18 is
               common enough that skipping the pad matters. */
            if (want < len)
                fseek(f, (long)(len - want), SEEK_CUR);

            if (len & 1)
                fseek(f, 1, SEEK_CUR);

            continue;
        }

        if (memcmp(ch, "data", 4) == 0)
        {
            if (!haveFmt)
            {
                fclose(f);
                why = "has its data before its format";
                return false;
            }

            if (channels < 1 || channels > 8)
            {
                fclose(f);
                why = "has a channel count this reader does not handle";
                return false;
            }

            if (!((format == 1 && (bits == 8 || bits == 16 || bits == 32)) ||
                  (format == 3 && bits == 32)))
            {
                fclose(f);
                why = "is not 8-, 16- or 32-bit PCM or 32-bit float";
                return false;
            }

            /* The multiplications are widened before they are done rather
               than after. Both are bounded by the checks above -- `bytes'
               is 4 at the most and `channels' is 8 -- so neither can
               overflow in fact, but a multiply of two narrow operands
               assigned to a wide one is a shape worth not writing: it is
               right here only because of a bound five lines up, and the
               next person to widen that bound should not have to notice. */
            const unsigned bytes = bits / 8;
            const unsigned long stride = (unsigned long)bytes * channels;

            /* Against what the file holds, not just against what its
               header claims. `len' is a 32-bit field an author can put
               anything in, and it used to size the reserve below on its
               own: a 244-byte file declaring 0xfffffff0 asked for 8.6 GB,
               which a host with overcommit hands over and a 32-bit
               emscripten heap does not. */
            const long here = ftell(f);
            const unsigned long left =
                (here >= 0 && fileBytes > here)
                    ? (unsigned long)(fileBytes - here) : 0;
            const unsigned long have = (len < left) ? len : left;
            const unsigned long count = stride ? have / stride : 0;

            if (count > THINK_SAMPLE_FRAMES_MAX)
            {
                fclose(f);
                why = "is longer than this reader plays";
                return false;
            }

            std::vector<float> raw;

            raw.reserve(count);

            std::vector<unsigned char> frame(stride ? stride : 1);

            for (unsigned long i = 0; i < count; i++)
            {
                if (fread(&frame[0], 1, stride, f) != stride)
                    break;

                /* Summed and then scaled by the channel count: a stereo
                   file whose two sides are the same signal comes out at
                   the level it went in, and one whose sides differ comes
                   out quieter, which is what summing means. */
                double sum = 0;

                for (unsigned c = 0; c < channels; c++)
                {
                    const unsigned char *p = &frame[(size_t)c * bytes];

                    if (format == 3)
                    {
                        /* IEEE float, assembled and copied rather than
                           read through a float * -- the bytes are not
                           aligned and the cast would be a strict
                           aliasing violation besides. */
                        uint32_t bitsv = (uint32_t)thWavU32(p);
                        float v;

                        memcpy(&v, &bitsv, sizeof(v));
                        sum += v;
                    }
                    else if (bits == 8)
                        sum += ((int)p[0] - 128) / 128.0;    /* unsigned */
                    else if (bits == 16)
                        sum += (double)(int16_t)thWavU16(p) / 32768.0;
                    else
                        sum += (double)(int32_t)thWavU32(p) / 2147483648.0;
                }

                raw.push_back((float)(sum / channels));
            }

            fclose(f);

            if (fileRate == 0 || fileRate == rate || raw.empty())
            {
                out.frames.swap(raw);
                return true;
            }

            /* A file at another rate, by ratio, once. Linear, which is
               what the playback does between frames anyway. */
            const double ratio = (double)rate / (double)fileRate;
            const double want = (double)raw.size() * ratio;

            /* `fileRate' is whatever the header said, so `want' is not
               bounded by anything the file actually contains: 1 Hz turns a
               hundred frames into a hundred seconds. Checked in double,
               before the cast, because the cast is what would lose it. */
            if (!(want <= (double)THINK_SAMPLE_FRAMES_MAX))
            {
                why = "is longer than this reader plays once resampled";
                return false;
            }

            const size_t n = (size_t)want;

            out.frames.resize(n ? n : 1);

            for (size_t i = 0; i < out.frames.size(); i++)
            {
                const double at = (double)i / ratio;
                const size_t k = (size_t)at;
                const float a = raw[(k < raw.size()) ? k : raw.size() - 1];
                const float b = raw[(k + 1 < raw.size()) ? k + 1
                                                        : raw.size() - 1];

                out.frames[i] = a + (b - a) * (float)(at - (double)k);
            }

            return true;
        }

        /* Anything else -- LIST, fact, cue, a DAW's own -- skipped, plus
           the pad byte an odd length carries. */
        if (fseek(f, (long)(len + (len & 1)), SEEK_CUR) != 0)
            break;
    }

    fclose(f);
    why = "has no data chunk";

    return false;
}

/* The frames for `name', reading it the first time it is asked for. NULL
   only when the plugin has no table, which cannot happen for a node the
   synth built. An entry with no frames in it is a file that would not
   read; the caller plays silence. */
static inline const thSampleData *
thSampleGet (const thPlugin *plugin, const std::string &name, unsigned rate)
{
    thSampleSlot *slot = thSampleSlotFor(plugin);

    if (slot == NULL || slot->table == NULL)
        return NULL;

    std::map<std::string, thSampleData>::const_iterator i =
        slot->table->find(name);

    if (i != slot->table->end())
        return &i->second;

    thSampleData &entry = (*slot->table)[name];

    if (name.empty())
    {
        fprintf(stderr, "osc::sample: no `file' was named\n");
        return &entry;
    }

    /* Found the way a .dsp is, which is what makes a piece sendable: a
       kit that only loaded from the directory it was built in is a kit
       you cannot mail anybody. `samples/' rather than the top of the
       search path so that `kick.wav' and a patch called `kick.dsp' do
       not share a namespace. */
    const std::string path =
        thUtil::findDataFile("samples/" + name, "dsp", "THINK_DSP_PATH",
                             DSP_PATH);
    std::string why;

    if (path.empty())
        fprintf(stderr, "osc::sample: '%s' not found on THINK_DSP_PATH "
                "under samples/\n", name.c_str());
    else if (!thWavRead(path, rate, entry, why))
        fprintf(stderr, "osc::sample: '%s' %s\n", path.c_str(), why.c_str());

    return &entry;
}

#endif /* THINK_SAMPLESLOT_H */
