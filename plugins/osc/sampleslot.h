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

#ifndef THINK_SAMPLESLOT_H
#define THINK_SAMPLESLOT_H

/* The wavs osc::sample plays, read once and shared by every voice.
 *
 * ONE TABLE PER SYNTH, keyed on the thPlugin the synth's plugin manager
 * loaded -- osc/noiseslot.h's arrangement, for osc/noiseslot.h's reason.
 * Every synth loads its own thPlugin for a given file and every node
 * built from it reaches that pointer through node->plugin(), so a slot
 * claimed in module_init and keyed on it is a table that one synth owns.
 * Sixteen voices of a drum
 * graph share one copy of the drum; two synths do not share anything,
 * which is what keeps a render reproducible.
 *
 * WHERE THE READ HAPPENS: at load, on the loading thread. Each plugin's
 * module_prepare primes the table with every file its node names, and the
 * synth prepares every graph again when the host says data files have
 * changed (thSynth::prepareTrees). The audio thread only looks the table
 * up, without a lock: it is published whole and never changed after, so
 * an addition is a new table, and the old ones are kept until the synth
 * releases the plugin. A name nobody primed -- a graph built without a
 * synth's load -- is still read on the first window that asks for it,
 * which is a fopen, a whole decode and a resample on the thread that is
 * supposed to be handing back a buffer.
 *
 * A FAILURE IS CACHED TOO. Without that, a graph naming a file that is
 * not there would stat() once per window per voice forever and print a
 * line each time. The entry is remembered with no frames in it, which is
 * the same thing the reader returns for an empty file, and both play as
 * silence -- until the host says new files have arrived
 * (thUtil::dataFilesChanged) and the graph is prepared again, when it is
 * read once more. That is a sample pack downloaded while an instrument
 * naming it is loaded. A file the host says it wrote over
 * (thUtil::dataFileChanged) is read again even if it was found: a pack
 * updated while it plays.
 *
 * ZONES. A `file' of the form "a.wav@48 b.wav@55 c.wav@62" is a set of
 * recordings with the MIDI note each was made at; thSampleZoneFor picks
 * the one nearest a note, the lower on a tie. The list is parsed once per
 * synth and kept beside the wavs.
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
#include <math.h>
#include <mutex>
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
    unsigned generation = 0;    /* thUtil::dataFilesGeneration, checked at */
};

/* One recording of a zoned `file', and the frequency it was made at. */
struct thSampleZone
{
    std::string file;
    double root;
};

/* A `file' text as a node names it: its zones, none for a plain name, and
   the next velocity layer of the voices playing it. All voices in a synth
   reach the same one, so alternate hits do not restart at layer one. */
struct thSampleText
{
    std::vector<thSampleZone> zones;
    std::atomic<unsigned char> next{0};
};

/* What the audio thread reads, without a lock: never changed once it is
   published, so an addition is a new table. */
struct thSampleTable
{
    std::map<std::string, thSampleData *> files;
    std::map<std::string, thSampleText *> texts;
};

struct thSampleSlot
{
    std::atomic<const thPlugin *>      owner;
    std::atomic<const thSampleTable *> table;

    /* Held by whoever adds to the table: the loading thread priming it, or
       the audio thread on a name nobody primed. Everything below is the
       holder's, and lives until the synth releases the plugin -- a table
       or a file replaced may still be read by the window rendering. */
    std::mutex adding;
    std::vector<thSampleTable *> tables;
    std::vector<thSampleData *> files;
    std::vector<thSampleText *> texts;
};

static thSampleSlot thSampleSlots[THINK_SAMPLE_SLOTS];

static inline thSampleSlot *thSampleSlotFor (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
        if (thSampleSlots[i].owner.load(std::memory_order_acquire) == plugin)
            return &thSampleSlots[i];

    return NULL;
}

/* Holding `adding'. */
static inline void thSampleFree (thSampleSlot *slot)
{
    for (thSampleTable *t : slot->tables)
        delete t;

    for (thSampleData *d : slot->files)
        delete d;

    for (thSampleText *t : slot->texts)
        delete t;

    slot->tables.clear();
    slot->files.clear();
    slot->texts.clear();
    slot->table.store(NULL, std::memory_order_release);
}

/* Holding `adding': `next' is what the audio thread reads from now. */
static inline void thSamplePublish (thSampleSlot *slot, thSampleTable *next)
{
    slot->tables.push_back(next);
    slot->table.store(next, std::memory_order_release);
}

static inline void thSampleClaim (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
    {
        const thPlugin *none = NULL;

        if (thSampleSlots[i].owner.compare_exchange_strong(
                none, plugin, std::memory_order_acq_rel))
        {
            std::lock_guard<std::mutex> hold(thSampleSlots[i].adding);

            thSampleFree(&thSampleSlots[i]);
            thSamplePublish(&thSampleSlots[i], new thSampleTable());
            return;
        }
    }
}

static inline void thSampleRelease (const thPlugin *plugin)
{
    for (int i = 0; i < THINK_SAMPLE_SLOTS; i++)
        if (thSampleSlots[i].owner.load(std::memory_order_acquire) == plugin)
        {
            {
                std::lock_guard<std::mutex> hold(thSampleSlots[i].adding);

                thSampleFree(&thSampleSlots[i]);
            }

            thSampleSlots[i].owner.store(NULL, std::memory_order_release);
            return;
        }
}

/* ---- the reader ---------------------------------------------------------
 *
 * Sixty-odd lines of RIFF walk rather than a library, because what is
 * needed is PCM, float and a chunk loop, and a dependency that has to
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

/* What a wav is read from: an open file, or bytes already in memory -- a
 * sample compiled into a plugin (thUtil::findEmbeddedFile). The four
 * things the reader does with a file, and nothing else, so the parser
 * below is one piece of code for both. */
struct thWavIn
{
    FILE *f;
    const unsigned char *data;
    size_t size, pos;

    size_t read (void *to, size_t n)
    {
        if (f != NULL)
            return fread(to, 1, n, f);

        const size_t k = (n < size - pos) ? n : size - pos;

        memcpy(to, data + pos, k);
        pos += k;

        return k;
    }

    /* Forward by `n'. A file seeks past its end happily and the next read
       comes back short, which is what ends the reader; memory does the
       same by stopping at its end. */
    bool skip (long n)
    {
        if (f != NULL)
            return fseek(f, n, SEEK_CUR) == 0;

        pos = (n < 0 || (size_t)n > size - pos) ? size : pos + (size_t)n;

        return true;
    }

    long tell (void)
    {
        return f != NULL ? ftell(f) : (long)pos;
    }

    void close (void)
    {
        if (f != NULL)
            fclose(f);

        f = NULL;
    }
};

/* Reads a wav from `in' into `out', mono, resampled to `rate'. `fileBytes'
   is how many bytes `in' holds, so that a `data' chunk header claiming
   more than there is cannot size an allocation. False and an empty `out'
   on anything it does not understand; `why' says which. */
static inline bool thWavParse (thWavIn &in, long fileBytes, unsigned rate,
                               thSampleData &out, std::string &why)
{
    out.frames.clear();

    unsigned char head[12];

    if (in.read(head, 12) != 12 ||
        memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0)
    {
        in.close();
        why = "is not a RIFF/WAVE file";
        return false;
    }

    unsigned channels = 0, bits = 0, format = 0;
    unsigned long fileRate = 0;
    bool haveFmt = false;

    for (;;)
    {
        unsigned char ch[8];

        if (in.read(ch, 8) != 8)
            break;                              /* end of the chunks */

        const unsigned long len = thWavU32(ch + 4);

        if (memcmp(ch, "fmt ", 4) == 0 && len >= 16)
        {
            unsigned char fmt[40];
            const unsigned long want = (len > sizeof(fmt)) ? sizeof(fmt) : len;

            if (in.read(fmt, want) != want)
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

            /* Past 2^31 a skip goes backwards where long is 32 bits; see
               the skip at the end of the loop. */
            if (len >= 0x7ffffffful)
                break;

            /* Chunks are padded to an even length, and a `fmt ' of 18 is
               common enough that skipping the pad matters. */
            if (want < len)
                in.skip((long)(len - want));

            if (len & 1)
                in.skip(1);

            continue;
        }

        if (memcmp(ch, "data", 4) == 0)
        {
            if (!haveFmt)
            {
                in.close();
                why = "has its data before its format";
                return false;
            }

            if (channels < 1 || channels > 8)
            {
                in.close();
                why = "has a channel count this reader does not handle";
                return false;
            }

            if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24 ||
                                   bits == 32)) ||
                  (format == 3 && bits == 32)))
            {
                in.close();
                why = "is not 8-, 16-, 24- or 32-bit PCM or 32-bit float";
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
            const long here = in.tell();
            const unsigned long left =
                (here >= 0 && fileBytes > here)
                    ? (unsigned long)(fileBytes - here) : 0;
            const unsigned long have = (len < left) ? len : left;
            const unsigned long count = stride ? have / stride : 0;

            if (count > THINK_SAMPLE_FRAMES_MAX)
            {
                in.close();
                why = "is longer than this reader plays";
                return false;
            }

            std::vector<float> raw;

            raw.reserve(count);

            std::vector<unsigned char> frame(stride ? stride : 1);

            for (unsigned long i = 0; i < count; i++)
            {
                if (in.read(&frame[0], stride) != stride)
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

                        /* A NaN or an infinity costs the voice that
                           plays it; silence costs a sample. */
                        sum += thIsFinite(v) ? v : 0;
                    }
                    else if (bits == 8)
                        sum += ((int)p[0] - 128) / 128.0;    /* unsigned */
                    else if (bits == 16)
                        sum += (double)(int16_t)thWavU16(p) / 32768.0;
                    else if (bits == 24)
                        sum += (double)(int32_t)((uint32_t)p[0] << 8 |
                                                 (uint32_t)p[1] << 16 |
                                                 (uint32_t)p[2] << 24) /
                               2147483648.0;
                    else
                        sum += (double)(int32_t)thWavU32(p) / 2147483648.0;
                }

                raw.push_back((float)(sum / channels));
            }

            in.close();

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
           the pad byte an odd length carries. A length a long cannot hold
           -- 2 GB, where long is 32 bits, as on Windows -- would be a skip
           backwards into the chunk just read, and round again for ever;
           a chunk that long is past the end of any file this reads. */
        if (len >= 0x7ffffffful || !in.skip((long)(len + (len & 1))))
            break;
    }

    in.close();
    why = "has no data chunk";

    return false;
}

/* Reads `path' into `out', mono, resampled to `rate'. */
static inline bool thWavRead (const std::string &path, unsigned rate,
                              thSampleData &out, std::string &why)
{
    thWavIn in = { fopen(path.c_str(), "rb"), NULL, 0, 0 };

    out.frames.clear();

    if (in.f == NULL)
    {
        why = "could not be opened";
        return false;
    }

    /* How many bytes there actually are, so that a `data' chunk header
       claiming more than the file holds cannot size an allocation. A
       truncated or corrupt wav is a thing that happens; reserving the
       4 GB its length field asks for is not a thing that should. */
    long fileBytes = 0;

    if (fseek(in.f, 0, SEEK_END) == 0)
        fileBytes = ftell(in.f);

    if (fileBytes < 0 || fseek(in.f, 0, SEEK_SET) != 0)
    {
        in.close();
        why = "could not be measured";
        return false;
    }

    return thWavParse(in, fileBytes, rate, out, why);
}

/* The same, from bytes in memory. */
static inline bool thWavReadMemory (const unsigned char *data, size_t size,
                                    unsigned rate, thSampleData &out,
                                    std::string &why)
{
    thWavIn in = { NULL, data, size, 0 };

    return thWavParse(in, (long)size, rate, out, why);
}

/* `name' read into `entry', saying why on stderr if it would not. */
static inline void thSampleRead (const std::string &name, unsigned rate,
                                 thSampleData &entry)
{
    if (name.empty())
    {
        fprintf(stderr, "osc::sample: no `file' was named\n");
        return;
    }

    /* Found the way a .dsp is, which is what makes a piece sendable: a
       kit that only loaded from the directory it was built in is a kit
       you cannot mail anybody. `samples/' rather than the top of the
       search path so that `kick.wav' and a patch called `kick.dsp' do
       not share a namespace. */
    std::string why;

    /* A sample the host carries inside itself -- an audio plugin built
       around one .dsp -- before anything on disk. */
    const unsigned char *bytes = NULL;
    size_t size = 0;

    if (thUtil::findEmbeddedFile("samples/" + name, bytes, size))
    {
        if (!thWavReadMemory(bytes, size, rate, entry, why))
            fprintf(stderr, "osc::sample: '%s' (embedded) %s\n",
                    name.c_str(), why.c_str());

        return;
    }

    const std::string path =
        thUtil::findDataFile("samples/" + name, "dsp", "THINK_DSP_PATH",
                             DSP_PATH);

    if (path.empty())
        fprintf(stderr, "osc::sample: '%s' not found on THINK_DSP_PATH "
                "under samples/\n", name.c_str());
    else if (!thWavRead(path, rate, entry, why))
        fprintf(stderr, "osc::sample: '%s' %s\n", path.c_str(), why.c_str());
}

/* Holding `adding'. The table's entry for `name', read now if there is
   none, or if data files have changed since it was checked and it was
   not found or has been written over. */
static inline thSampleData *thSampleFile (thSampleSlot *slot,
                                          const std::string &name,
                                          unsigned rate)
{
    const thSampleTable *table = slot->table.load(std::memory_order_relaxed);
    const auto i = table->files.find(name);
    const unsigned generation = thUtil::dataFilesGeneration();

    if (i != table->files.end())
    {
        thSampleData *kept = i->second;

        if (kept->generation == generation)
            return kept;

        if (!kept->frames.empty() &&
            thUtil::dataFileGeneration("samples/" + name) <= kept->generation)
        {
            kept->generation = generation;
            return kept;
        }
    }

    thSampleData *entry = new thSampleData();

    entry->generation = generation;
    slot->files.push_back(entry);
    thSampleRead(name, rate, *entry);

    thSampleTable *next = new thSampleTable(*table);

    next->files[name] = entry;
    thSamplePublish(slot, next);

    return entry;
}

/* Audio thread. The frames for `name'. NULL only when the plugin has no
   table, which cannot happen for a node the synth built. An entry with no
   frames in it is a file that would not read; the caller plays silence.
   A name nobody primed is read here, on the audio thread. */
static inline const thSampleData *
thSampleGet (const thPlugin *plugin, const std::string &name, unsigned rate)
{
    thSampleSlot *slot = thSampleSlotFor(plugin);
    const thSampleTable *table =
        slot ? slot->table.load(std::memory_order_acquire) : NULL;

    if (table == NULL)
        return NULL;

    const auto i = table->files.find(name);

    if (i != table->files.end())
        return i->second;

    std::lock_guard<std::mutex> hold(slot->adding);

    return thSampleFile(slot, name, rate);
}

/* Zoned if some word ends in `@' and a note number: a plain file name may
   have an `@' anywhere else. */
static inline bool thSampleIsZoned (const std::string &text)
{
    for (size_t at = text.find('@'); at != std::string::npos;
         at = text.find('@', at + 1))
    {
        const size_t end = text.find_first_of(" \t\n,", at);
        const std::string num =
            text.substr(at + 1, end == std::string::npos ? std::string::npos
                                                         : end - at - 1);
        char *stop = NULL;

        if (!num.empty())
        {
            strtod(num.c_str(), &stop);
            if (*stop == 0)
                return true;
        }
    }

    return false;
}

/* Holding `adding'. The table's entry for the text `text', its zones
   parsed if it has them. */
static inline thSampleText *thSampleTextFor (thSampleSlot *slot,
                                             const std::string &text)
{
    const thSampleTable *table = slot->table.load(std::memory_order_relaxed);
    const auto i = table->texts.find(text);

    if (i != table->texts.end())
        return i->second;

    thSampleText *entry = new thSampleText();
    size_t at = thSampleIsZoned(text) ? 0 : text.size();

    slot->texts.push_back(entry);

    while (at < text.size())
    {
        const size_t end = text.find_first_of(" \t\n,", at);
        const std::string word =
            text.substr(at, end == std::string::npos ? std::string::npos
                                                     : end - at);
        const size_t mark = word.rfind('@');

        if (mark != std::string::npos && mark > 0)
        {
            const double midi = atof(word.c_str() + mark + 1);
            entry->zones.push_back({word.substr(0, mark),
                                    440.0 * pow(2.0, (midi - 69) / 12.0)});
        }
        else if (!word.empty())
        {
            fprintf(stderr, "osc::sample: zone '%s' has no @note\n",
                    word.c_str());
        }

        if (end == std::string::npos)
            break;
        at = end + 1;
    }

    thSampleTable *next = new thSampleTable(*table);

    next->texts[text] = entry;
    thSamplePublish(slot, next);

    return entry;
}

/* Audio thread. The table's entry for `text', made here if nobody primed
   it. NULL when the plugin has no table. */
static inline thSampleText *thSampleTextGet (const thPlugin *plugin,
                                             const std::string &text)
{
    thSampleSlot *slot = thSampleSlotFor(plugin);
    const thSampleTable *table =
        slot ? slot->table.load(std::memory_order_acquire) : NULL;

    if (table == NULL)
        return NULL;

    const auto i = table->texts.find(text);

    if (i != table->texts.end())
        return i->second;

    std::lock_guard<std::mutex> hold(slot->adding);

    return thSampleTextFor(slot, text);
}

/* Loading thread: every file the text `text' names, read into the table
   or checked again, so that the voices playing it read nothing. */
static inline void thSamplePrime (const thPlugin *plugin,
                                  const std::string &text, unsigned rate)
{
    thSampleSlot *slot = thSampleSlotFor(plugin);

    if (slot == NULL)
        return;

    std::lock_guard<std::mutex> hold(slot->adding);

    if (slot->table.load(std::memory_order_relaxed) == NULL)
        return;

    const thSampleText *entry = thSampleTextFor(slot, text);

    if (entry->zones.empty())
        thSampleFile(slot, text, rate);

    for (const thSampleZone &zone : entry->zones)
        thSampleFile(slot, zone.file, rate);
}

/* Audio thread. Counts a trigger once across every voice using this
   primary file. */
static inline unsigned thSampleNextLayer (const thPlugin *plugin,
                                          const std::string &name)
{
    thSampleText *entry = thSampleTextGet(plugin, name);

    if (entry == NULL)
        return 0;

    const unsigned chosen = entry->next.load(std::memory_order_relaxed);

    entry->next.store((unsigned char)((chosen + 1) % 3),
                      std::memory_order_relaxed);

    return chosen;
}

/* The zone of a zoned `file' nearest `note', or NULL for a list with no
   zones in it. */
static inline const thSampleZone *
thSampleZoneFor (const thPlugin *plugin, const std::string &text,
                 double note)
{
    const thSampleText *entry = thSampleTextGet(plugin, text);

    if (entry == NULL)
        return NULL;

    const thSampleZone *best = NULL;
    double bestDistance = 0;
    const double freq = 440.0 * pow(2.0, (note - 69) / 12.0);

    for (const thSampleZone &zone : entry->zones)
    {
        const double distance = fabs(log2(freq / zone.root));

        if (best == NULL || distance < bestDistance - 1e-9 ||
            (fabs(distance - bestDistance) <= 1e-9 && zone.root < best->root))
        {
            best = &zone;
            bestDistance = distance;
        }
    }

    return best;
}

#endif /* THINK_SAMPLESLOT_H */
