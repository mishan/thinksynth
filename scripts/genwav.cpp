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

/*
 * genwav -- render a .gen to a file, with no window and no sound card.
 *
 *   scripts/genwav -p plugins/ -s 180 -o ebb.wav gen/ebb.gen
 *   scripts/genwav -p plugins/ -t - gen/round.gen | head
 *   scripts/genwav -p plugins/ -s 180 --midi anthem.mid gen/anthem.gen
 *   scripts/genwav -p plugins/ -s 60 --knob pump=0,1 gen/warehouse.gen
 *
 * gencheck drives the scheduler through its virtual clock and keeps the
 * delivered events as a tape, which is how it proves a piece replays. This
 * does the same thing and keeps the audio as well: after every step of the
 * clock the synth renders one window, and the windows are written out as
 * 16-bit PCM. Nothing here is real time, so a three-minute piece takes a
 * few seconds, and a piece that would clip does so in a file rather than
 * in your ears.
 *
 * What it is for: hearing a piece without opening it in the program,
 * checking a new instrument block's level against the others before
 * pressing Play, and reading the tape -- `-t' -- to see what a chain
 * actually delivered when the piano roll is not to hand. The summary
 * printed at the end (peak, RMS, clipped samples, notes delivered) is the
 * number a level question wants.
 *
 * `--profile' times every window as it renders, and says which ones
 * would not have kept up with a sound card: the mean, the 99th
 * percentile and the worst against a window's own length, the slowest
 * second, which is what a slow machine runs out of, the worst windows
 * with their sections and the channels that cost the most in them, and
 * each channel's share. The times are this machine's; what carries to
 * another is where the cost is and how it moves through the piece.
 *
 * `--midi' writes the same delivered events as a Standard MIDI File, a
 * track per chain at the piece's tempo, for taking a piece into a DAW
 * (thcMidiFile says what each event becomes). Its times are the
 * scheduled ones too, not the window boundaries.
 *
 * The clock steps one audio window at a time -- 1024 samples, about
 * twenty-three milliseconds at the default rate -- so an event lands on the
 * window boundary after its scheduled time. The program's composer delivers
 * on a twenty-millisecond timer and has the same granularity; the file
 * sounds the way the program does, and the tape's times are the scheduled
 * ones.
 *
 * Instruments load the way they do everywhere else: the `dsp' name is
 * searched for under THINK_DSP_PATH, the current directory's dsp/, and the
 * install prefix, in that order. Run it from the source tree and the tree's
 * dsp/ is what it finds. A piece whose sinks name channels rather than
 * instruments renders those channels silent, since nothing is loaded on
 * them -- that is what such a piece does before somebody aims the channel,
 * and this tool has no somebody.
 *
 * `--knob NAME=A' sets a piece knob before Play. `--knob NAME=A,B' renders
 * the piece a second time at B, in step with the first, and `--levels'
 * gains the RMS of the difference between the two per channel: zero is a
 * knob that does nothing to that channel. Only the first render is written.
 *
 * After the transport stops the render continues until the tails have rung
 * out, up to a few seconds, so a long release is in the file rather than
 * cut off at the length you asked for.
 *
 * `--json FILE' writes what the summary, `--levels' and `--sections' say
 * as one JSON object (`-' for stdout), with the load's errors and
 * warnings, for a program to read rather than a person. `--check' loads
 * the piece and describes it -- knobs, chains, instruments, sections --
 * without rendering. `--knob-at T:NAME=V' moves a knob T seconds in, as a
 * host would while the piece plays; give it as often as the timeline
 * needs. `--stems DIR' writes each channel's own sound beside the mix,
 * one WAV per instrument, as `--levels' measures it. `--stages' prints
 * every composer plugin's params as JSON and needs no piece.
 *
 * With no `-p', a tree's own build is found beside this binary when the
 * install prefix has no plugins, so a fresh build runs as it stands.
 *
 * Exit status is 0; 3 if any sample reached full scale, which is the thing
 * this tool exists to catch; and 4, which wins, if the engine's per-voice
 * guard dropped a voice for going non-finite. Both are in the summary too.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "think.h"
#include "thPhoneme.h"

#include "libthink/thDynLib.h"
#include "thcPlugin.h"
#include "NodeEvolve.h"
#include "NodeGraft.h"
#include "NodeMutate.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcMidiExport.h"
#include "thcMidiFile.h"

#include "JsonOut.h"
#include "Loudness.h"

/* Enough silence to call a tail finished, and the longest we will wait
   for one. amb01's release tops out at five seconds; anything longer than
   TAIL_MAX is a drone, not a tail. */
#define TAIL_SILENT   1e-4f
#define TAIL_MAX      8.0

static void usage (const char *argv0)
{
    printf("usage: %s [-p PATH] [-s SECONDS] [-o FILE.wav] [-t FILE] "
           "[--midi FILE.mid] file.gen\n"
           "\n"
           "  -p, --plugin-path PATH  where to find plugin .so files\n"
           "  -s, --seconds N         how long to run the transport (default 120)\n"
           "  -o, --output FILE       write the audio here, 16-bit PCM WAV\n"
           "  -t, --tape FILE         write the delivered events here (- for stdout)\n"
           "      --midi FILE         write the delivered events here, as a MIDI file\n"
           "      --midi-fine         14-bit controllers for chanargs in the MIDI file\n"
           "      --levels            peak, RMS and loudness (LUFS) by "
           "instrument\n"
           "                          channel and for the mix\n"
           "      --sections          mix RMS by arrangement section\n"
           "      --profile           what each window cost to render, and "
           "where\n"
           "  -j, --threads N         render channels on N more threads; "
           "the same\n"
           "                          samples as on one\n"
           "  -m, --mono              sum the channels into one, for a sample\n"
           "      --from N            write the audio from N seconds in\n"
           "      --length N          and only N seconds of it, for a loop\n"
           "      --knob NAME=A[,B]   set a piece knob; with B, compare a "
           "render at B by channel\n"
           "      --knob-at T:NAME=V  move a knob to V at T seconds; repeatable\n"
           "      --json FILE         the summary, levels and sections as JSON "
           "(- for stdout)\n"
           "      --check             load and describe the piece; no render\n"
           "      --stems DIR         one WAV per instrument channel, in DIR\n"
           "      --stages            every composer plugin's params, as JSON\n"
           "  -q, --quiet             no summary\n",
           argv0);
}

static void loadComposers (const std::string &pluginDir,
                           std::map<std::string, thcPlugin *> &out)
{
    std::filesystem::path root =
        std::filesystem::path(pluginDir) / "composer";
    std::error_code ec;

    if (!std::filesystem::is_directory(root, ec))
        return;

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        out[p->name()] = p;

        /* Pin the module's mapping, and its dependency closure, for the life
           of the process: a second dlopen bumps the loader's reference count
           and this handle is never closed. Without it the dlclose in
           ~thcPlugin below can drop the last reference to the glib stack --
           which some cairo builds pull in -- and unmap the once-per-process
           init heap glib documents as never freed, turning it into
           LeakSanitizer reports naming "<unknown module>". gencheck makes the
           same arrangement and says more about why. */
        thDynLib::open(f.path().string());
    }
}

/* Frees the composer modules on every path out of main().
 *
 * They used to be left for exit() to deal with, on the grounds that this is a
 * tool rather than a test. It is a test now -- the ctest that renders
 * scripts/guard/nonfinite.gen through it -- and under the address sanitizer
 * that shortcut is twenty-five leak reports. */
struct HeldComposers {
    std::map<std::string, thcPlugin *> &plugins;

    ~HeldComposers (void)
    {
        for (std::map<std::string, thcPlugin *>::iterator i = plugins.begin();
             i != plugins.end(); ++i)
        {
            delete i->second;
        }
    }
};

/* One line per delivered event, in gencheck's spelling minus the
   seventeen digits: N time channel note velocity duration level, and the
   four aux after it only when one is not zero, and `say=' and its phonemes
   only when the note says something -- so a piece that sets none prints the
   tape it always did. C for a chanarg, P for a swap, E for a
   node-arg edit. Channels are the engine's, counted from zero; the tape
   names those channels first. wasm/tape.mjs's tapeLine is the same line. */
static void writeEvent (FILE *tape, const thcEvent &ev)
{
    switch (ev.type)
    {
        case THC_EV_NOTE:
        {
            const float *aux = ev.u.note.aux;

            fprintf(tape, "N %.3f %d %d %d %.3f %.3f", ev.at, ev.channel,
                    ev.u.note.note, ev.u.note.velocity,
                    ev.u.note.duration, (double)ev.u.note.level);

            if (aux[0] != 0 || aux[1] != 0 || aux[2] != 0 || aux[3] != 0)
                fprintf(tape, " %.3f %.3f %.3f %.3f", (double)aux[0],
                        (double)aux[1], (double)aux[2], (double)aux[3]);

            if (ev.u.note.say[0])
            {
                char said[8 * THC_NOTE_SAY];

                thPhonemeSpell(ev.u.note.say, THC_NOTE_SAY - 1, said,
                               sizeof(said));
                fprintf(tape, " say=%s", said);
            }

            fputc('\n', tape);
            break;
        }
        case THC_EV_CHANARG:
            fprintf(tape, "C %.3f %d %s %.4f\n", ev.at, ev.channel,
                    ev.u.chanarg.name ? ev.u.chanarg.name : "",
                    (double)ev.u.chanarg.value);
            break;
        case THC_EV_PATCH:
            fprintf(tape, "P %.3f %d %s\n", ev.at, ev.channel,
                    ev.u.patch.name ? ev.u.patch.name : "");
            break;
        case THC_EV_NODEARG:
            fprintf(tape, "E %.3f %d %s.%s %.4f\n", ev.at, ev.channel,
                    ev.u.nodearg.node ? ev.u.nodearg.node : "",
                    ev.u.nodearg.arg ? ev.u.nodearg.arg : "",
                    (double)ev.u.nodearg.value);
            break;
        default:
            fprintf(tape, "? %.3f %d %d\n", ev.at, ev.channel,
                    (int)ev.type);
            break;
    }
}

static bool writeWav (const std::string &path, const std::vector<float> &pcm,
                      int channels, int rate)
{
    FILE *w = fopen(path.c_str(), "wb");

    if (w == NULL)
        return false;

    const uint32_t dataBytes = (uint32_t)pcm.size() * 2;
    const uint16_t fmt = 1, ch = (uint16_t)channels, bits = 16;
    const uint16_t align = ch * 2;
    const uint32_t byteRate = (uint32_t)rate * align;
    const uint32_t fmtLen = 16, riffLen = 36 + dataBytes;
    const uint32_t rate32 = (uint32_t)rate;

    fwrite("RIFF", 1, 4, w);  fwrite(&riffLen, 4, 1, w);
    fwrite("WAVE", 1, 4, w);
    fwrite("fmt ", 1, 4, w);  fwrite(&fmtLen, 4, 1, w);
    fwrite(&fmt, 2, 1, w);    fwrite(&ch, 2, 1, w);
    fwrite(&rate32, 4, 1, w); fwrite(&byteRate, 4, 1, w);
    fwrite(&align, 2, 1, w);  fwrite(&bits, 2, 1, w);
    fwrite("data", 1, 4, w);  fwrite(&dataBytes, 4, 1, w);

    /* Clamped here as well as in the engine, because the engine's clamp is
       what thClampSample promises the output stage and this is a second
       output stage. */
    for (size_t i = 0; i < pcm.size(); i++)
    {
        float v = pcm[i];

        if (v > 1)  v = 1;
        if (v < -1) v = -1;

        const int16_t s = (int16_t)lrintf(v * 32767);

        fwrite(&s, 2, 1, w);
    }

    return fclose(w) == 0;
}

struct Level
{
    double sumsq = 0;
    size_t count = 0;
    float peak = 0;

    void add (float value)
    {
        const float a = fabsf(value);

        if (a > peak)
            peak = a;

        sumsq += (double)value * value;
        count++;
    }

    double rms (void) const { return count ? sqrt(sumsq / count) : 0; }
};

/* Integrated loudness to a tenth, or `-inf' for a channel that never
   rose above the meter's gate. */
static std::string lufsText (const Loudness *loud)
{
    const double l = loud ? loud->integrated() : -HUGE_VAL;
    char text[32];

    if (!isfinite(l))
        return "-inf";

    snprintf(text, sizeof(text), "%.1f", l);

    return text;
}

/* A WAV written as it renders, for --stems: a piece's channels held whole
   in memory would be a gigabyte for a long one. The header's lengths are
   filled in when it closes. Only the frames in [first, last) are kept, so
   a stem covers what --from and --length leave of the mix. */
struct Stem
{
    FILE  *file = NULL;
    int    outputs = 0;
    size_t samples = 0, seen = 0, first = 0, last = SIZE_MAX;

    bool open (const std::string &path, int channels, int rate)
    {
        outputs = channels;

        if (!writeWav(path, std::vector<float>(), channels, rate))
            return false;

        file = fopen(path.c_str(), "r+b");

        return file != NULL && fseek(file, 0, SEEK_END) == 0;
    }

    void add (const float *frames, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            const size_t frame = seen + i / (size_t)outputs;

            if (frame < first || frame >= last)
                continue;

            const float v = std::max(-1.0f, std::min(1.0f, frames[i]));
            const int16_t s = (int16_t)lrintf(v * 32767);

            fwrite(&s, 2, 1, file);
            samples++;
        }

        seen += n / (size_t)outputs;
    }

    bool close (void)
    {
        const uint32_t data = (uint32_t)samples * 2, riff = 36 + data;

        fseek(file, 4, SEEK_SET);
        fwrite(&riff, 4, 1, file);
        fseek(file, 40, SEEK_SET);
        fwrite(&data, 4, 1, file);

        return fclose(file) == 0;
    }
};

static void jsonKey (std::string &out, const char *key)
{
    if (out.back() != '{')
        out += ',';

    jsonString(out, key);
    out += ':';
}

static void jsonStrings (std::string &out, const std::vector<std::string> &v)
{
    out += '[';

    for (size_t i = 0; i < v.size(); i++)
    {
        if (i)
            out += ',';

        jsonString(out, v[i]);
    }

    out += ']';
}

static bool writeText (const std::string &path, const std::string &text)
{
    FILE *f = path == "-" ? stdout : fopen(path.c_str(), "w");

    if (f == NULL)
        return false;

    fputs(text.c_str(), f);
    fputc('\n', f);

    return f == stdout ? fflush(f) == 0 : fclose(f) == 0;
}

/* Every composer plugin and the params it registers, for a program that
   writes stages and wants to know what each one takes. */
static std::string stagesJson (const std::map<std::string, thcPlugin *> &plugins)
{
    static const char *types[] = { "float", "int", "note", "noteset",
                                   "string", "preset", "instrset" };
    std::string out = "[";

    for (const auto &p : plugins)
    {
        const thcPlugin *pl = p.second;

        if (out.size() > 1)
            out += ',';

        out += '{';
        jsonKey(out, "name");
        jsonString(out, pl->name());
        jsonKey(out, "kind");
        jsonString(out, pl->hasTick() && pl->hasReceive() ? "both"
                        : pl->hasTick() ? "gen" : "xform");
        jsonKey(out, "description");
        jsonString(out, pl->desc());
        jsonKey(out, "params");
        out += '[';

        for (int i = 0; i < pl->paramCount(); i++)
        {
            const thcPlugin::ParamInfo *in = pl->paramInfo(i);
            const bool text = in->type == THC_PARAM_NOTESET ||
                              in->type == THC_PARAM_STRING ||
                              in->type == THC_PARAM_PRESET ||
                              in->type == THC_PARAM_INSTRSET;

            if (i)
                out += ',';

            out += '{';
            jsonKey(out, "name");
            jsonString(out, in->name);
            jsonKey(out, "description");
            jsonString(out, in->desc);
            jsonKey(out, "type");
            jsonString(out, (size_t)in->type < sizeof types / sizeof *types
                            ? types[in->type] : "unknown");

            if (text)
            {
                jsonKey(out, "default");
                jsonString(out, in->defString);
            }
            else
            {
                jsonKey(out, "min");
                jsonNumber(out, in->min);
                jsonKey(out, "max");
                jsonNumber(out, in->max);
                jsonKey(out, "default");
                jsonNumber(out, in->def);
            }

            if (!in->units.empty())
            {
                jsonKey(out, "units");
                jsonString(out, in->units);
            }

            out += '}';
        }

        out += "]}";
    }

    return out + "]";
}

/* What a program writing a piece wants to know it said: its knobs, chains,
   instruments and sections, as loaded. */
static void describeJson (std::string &out, const thcGenLoader &loader,
                          thcScheduler &sched)
{
    jsonKey(out, "name");
    jsonString(out, loader.pieceName());
    jsonKey(out, "category");
    jsonString(out, loader.pieceCategory());
    jsonKey(out, "description");
    jsonString(out, loader.pieceDescription());
    jsonKey(out, "tempo");
    jsonNumber(out, sched.tempo());
    jsonKey(out, "meter");
    jsonNumber(out, sched.meter());
    jsonKey(out, "knobs");
    out += '[';

    for (const auto &k : sched.knobs())
    {
        if (out.back() != '[')
            out += ',';

        out += '{';
        jsonKey(out, "name");
        jsonString(out, k.first);
        jsonKey(out, "value");
        jsonNumber(out, (*k.second)[0]);
        jsonKey(out, "min");
        jsonNumber(out, k.second->min());
        jsonKey(out, "max");
        jsonNumber(out, k.second->max());
        jsonKey(out, "label");
        jsonString(out, k.second->label());
        out += '}';
    }

    out += ']';
    jsonKey(out, "chains");

    std::vector<std::string> chains;

    for (size_t c = 0; c < sched.chainCount(); c++)
        chains.push_back(sched.chain(c)->name);

    jsonStrings(out, chains);
    jsonKey(out, "instruments");
    out += '[';

    for (const thcInstrument &inst : sched.instruments())
    {
        if (out.back() != '[')
            out += ',';

        out += '{';
        jsonKey(out, "name");
        jsonString(out, inst.name);
        jsonKey(out, "dsp");
        jsonString(out, inst.dsp);
        jsonKey(out, "engine");
        jsonInt(out, inst.channel);
        out += '}';
    }

    out += ']';
    jsonKey(out, "sections");
    out += '[';

    for (const thcSection &sec : sched.sections())
    {
        if (out.back() != '[')
            out += ',';

        out += '{';
        jsonKey(out, "name");
        jsonString(out, sec.name);
        jsonKey(out, "seconds");
        jsonNumber(out, sched.sectionLength(sec));
        out += '}';
    }

    out += ']';
}

int main (int argc, char **argv)
{
    std::string pluginPath = PLUGIN_PATH;
    std::string genFile, wavFile, tapeFile, midiFile;
    bool mono = false, midiFine = false;
    double seconds = 120, from = 0, length = -1;
    bool quiet = false;
    bool levels = false, sections = false, profile = false;
    int threads = 0;
    std::string knobName;
    double knobA = 0, knobB = 0;
    bool compare = false;
    bool pathGiven = false, checkOnly = false, listStages = false;
    bool levelsText = false, sectionsText = false;
    std::string jsonFile, stemsDir;

    struct KnobMove { double at; std::string name; float value; };
    std::vector<KnobMove> moves;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--plugin-path"))
        {
            pathGiven = true;
            if (++i >= argc) { usage(argv[0]); return 2; }
            pluginPath = argv[i];
        }
        else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--seconds"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            seconds = atof(argv[i]);
        }
        else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            wavFile = argv[i];
        }
        else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--tape"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            tapeFile = argv[i];
        }
        else if (!strcmp(argv[i], "--midi"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            midiFile = argv[i];
        }
        else if (!strcmp(argv[i], "--midi-fine"))
            midiFine = true;
        else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--mono"))
            mono = true;
        else if (!strcmp(argv[i], "--from"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            from = atof(argv[i]);
        }
        else if (!strcmp(argv[i], "--length"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            length = atof(argv[i]);
        }
        else if (!strcmp(argv[i], "--knob"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }

            const char *eq = strchr(argv[i], '=');
            char *end = NULL;

            if (eq == NULL || eq == argv[i]) { usage(argv[0]); return 2; }

            knobName.assign(argv[i], eq - argv[i]);
            knobA = strtod(eq + 1, &end);

            if (end == eq + 1) { usage(argv[0]); return 2; }

            compare = *end == ',';

            if (compare)
            {
                const char *b = end + 1;

                knobB = strtod(b, &end);

                if (end == b) { usage(argv[0]); return 2; }
            }

            if (*end != '\0') { usage(argv[0]); return 2; }

            levels = levels || compare;
            levelsText = levelsText || compare;
        }
        else if (!strcmp(argv[i], "--knob-at"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }

            const char *colon = strchr(argv[i], ':');
            const char *eq = colon ? strchr(colon, '=') : NULL;
            char *end = NULL;

            if (eq == NULL || eq == colon + 1) { usage(argv[0]); return 2; }

            KnobMove m;

            m.at = strtod(argv[i], &end);

            if (end != colon || m.at < 0) { usage(argv[0]); return 2; }

            m.name.assign(colon + 1, eq - colon - 1);
            m.value = (float)strtod(eq + 1, &end);

            if (end == eq + 1 || *end != '\0') { usage(argv[0]); return 2; }

            moves.push_back(m);
        }
        else if (!strcmp(argv[i], "--json"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            jsonFile = argv[i];
            levels = sections = true;
        }
        else if (!strcmp(argv[i], "--check"))
            checkOnly = true;
        else if (!strcmp(argv[i], "--stems"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            stemsDir = argv[i];
        }
        else if (!strcmp(argv[i], "--stages"))
            listStages = true;
        else if (!strcmp(argv[i], "--levels"))
            levels = levelsText = true;
        else if (!strcmp(argv[i], "--sections"))
            sections = sectionsText = true;
        else if (!strcmp(argv[i], "--profile"))
            profile = true;
        else if (!strcmp(argv[i], "-j") || !strcmp(argv[i], "--threads"))
        {
            if (++i >= argc) { usage(argv[0]); return 2; }
            threads = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
            quiet = true;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            usage(argv[0]);
            return 0;
        }
        else if (genFile.empty())
            genFile = argv[i];
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if ((genFile.empty() && !listStages) || seconds <= 0)
    {
        usage(argv[0]);
        return 2;
    }

    if (wavFile.empty() && tapeFile.empty() && midiFile.empty() && quiet &&
        !levels && !sections && !checkOnly && !listStages &&
        stemsDir.empty())
    {
        fprintf(stderr, "%s: nothing to write and nothing to say\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    std::map<std::string, thcPlugin *> plugins;

    loadComposers(pluginPath, plugins);

    if (plugins.empty() && !pathGiven)
    {
        std::error_code ec;
        const std::filesystem::path beside = std::filesystem::weakly_canonical(
            std::filesystem::absolute(argv[0]).parent_path() / ".." /
            "plugins", ec);

        if (!ec)
        {
            pluginPath = beside.string() + "/";
            loadComposers(pluginPath, plugins);
        }
    }

    const HeldComposers held = { plugins };

    if (plugins.empty())
    {
        fprintf(stderr, "%s: no composer modules under %s -- build the "
                "plugins first, or pass -p\n", argv[0], pluginPath.c_str());
        return 2;
    }

    /* Built with the plugin path, because a piece's instruments are .dsp
       graphs and their nodes have to come from somewhere. */
    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thcScheduler sched(&synth);

    sched.setGrower([&synth] (const std::string &text,
                              const std::string &base, unsigned int choice,
                              const std::string &kinds, size_t want,
                              std::vector<std::string> &grown,
                              std::vector<std::string> &w,
                              std::string &why)
                    { return NodeMutate::grow(&synth, text, base, choice,
                                              kinds, want, grown, w, why); });
    sched.setTuner(NodeEvolve::tuneAdded);
    sched.setGrafter(nodeBreedGrafter(&synth));

    synth.setThreads(threads);

    /* A piece that listens replays exactly only if the ear answers
       inside the tick; offline, that costs nothing but time. */
    sched.setAuditionSynchronous(true);
    thcGenLoader loader(plugins);

    if (listStages)
        return writeText("-", stagesJson(plugins)) ? 0 : 1;

    const bool loaded = loader.load(genFile, &sched);

    if (!jsonFile.empty() && (!loaded || checkOnly))
    {
        std::string out = "{";

        jsonKey(out, "ok");
        out += loaded ? "true" : "false";
        jsonKey(out, "errors");
        jsonStrings(out, loader.errors());
        jsonKey(out, "warnings");
        jsonStrings(out, loader.warnings());

        if (loaded)
            describeJson(out, loader, sched);

        out += '}';

        if (!writeText(jsonFile, out))
            return 1;
    }

    if (!loaded)
    {
        for (size_t k = 0; k < loader.errors().size(); k++)
            fprintf(stderr, "%s\n", loader.errors()[k].c_str());

        return 1;
    }

    for (size_t k = 0; k < loader.warnings().size(); k++)
        fprintf(stderr, "%s\n", loader.warnings()[k].c_str());

    if (checkOnly)
        return 0;

    for (const KnobMove &m : moves)
        if (sched.knob(m.name) == NULL)
        {
            fprintf(stderr, "%s: %s declares no knob @%s\n", argv[0],
                    genFile.c_str(), m.name.c_str());
            return 2;
        }

    std::stable_sort(moves.begin(), moves.end(),
                     [] (const KnobMove &a, const KnobMove &b)
                     { return a.at < b.at; });

    /* The same piece on a synth of its own, for --knob A,B: the same seed,
       so the knob is the only thing the two renders disagree on. */
    std::unique_ptr<thSynth> otherSynth;
    std::unique_ptr<thcScheduler> other;
    thcGenLoader otherLoader(plugins);

    if (compare)
    {
        otherSynth.reset(new thSynth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                                     TH_DEFAULT_SAMPLES));
        otherSynth->setThreads(threads);
        other.reset(new thcScheduler(otherSynth.get()));
        other->setAuditionSynchronous(true);
        other->setMasterSeed(sched.masterSeed());

        if (!otherLoader.load(genFile, other.get()))
            return 1;
    }

    if (!knobName.empty())
    {
        thArg *knob = sched.knob(knobName);

        if (knob == NULL)
        {
            fprintf(stderr, "%s: %s declares no knob @%s\n", argv[0],
                    genFile.c_str(), knobName.c_str());
            return 2;
        }

        knob->setValue((float)knobA);

        if (other)
            other->knob(knobName)->setValue((float)knobB);
    }

    FILE *tape = NULL;

    if (tapeFile == "-")
        tape = stdout;
    else if (!tapeFile.empty())
    {
        tape = fopen(tapeFile.c_str(), "w");

        if (tape == NULL)
        {
            fprintf(stderr, "%s: cannot write %s\n", argv[0],
                    tapeFile.c_str());
            return 1;
        }
    }

    if (tape != NULL)
        for (const thcInstrument &inst : sched.instruments())
            if (inst.channel >= 0)
                fprintf(tape, "# channel %d = %s\n", inst.channel,
                        inst.name.c_str());

    /* At the piece's tempo, which the loader set and nothing changes while
       it plays; the sections at the times the scheduler gates them. */
    thcMidiFile midi(sched.tempo(), sched.meter());

    midi.setFineControllers(midiFine);
    thcMidiExport::describe(midi, sched, synth,
                            std::filesystem::path(genFile).stem().string());

    size_t notes = 0;

    sigc::connection conn = sched.sigDelivered.connect(
        [&](const thcEvent &ev)
        {
            if (ev.type == THC_EV_NOTE)
                notes++;

            if (tape != NULL)
                writeEvent(tape, ev);

            if (!midiFile.empty())
                midi.add(ev, sched.deliveringChain());
        });

    const int channels = synth.audioChannelCount();
    const int window = synth.getWindowlen();
    const double dt = (double)window / TH_DEFAULT_SAMPLES;
    const size_t frame = (size_t)channels * window;

    std::vector<float> pcm;
    std::vector<Level> channelLevels(levels ? synth.midiChanCount() : 0);
    std::vector<std::unique_ptr<Loudness> > channelLoudness(
        channelLevels.size());
    Loudness mixLoudness(channels, TH_DEFAULT_SAMPLES);
    std::vector<Level> sectionLevels(sections ? sched.sections().size() : 0);
    std::vector<Level> channelDiffs(channelLevels.size());
    Level mixDiff;

    /* A channel opened when it first sounds, with the windows before it
       written as silence, so every stem starts where the mix does. */
    std::vector<Stem> stems(stemsDir.empty() ? 0 : synth.midiChanCount());
    size_t windowsRendered = 0;

    /* The frames --from and --length keep. The meters read only these, so
       what they say is what the files written hold. */
    const size_t keepFirst = (size_t)(std::max(from, 0.0) *
                                      TH_DEFAULT_SAMPLES);
    const size_t keepLast = length < 0 ? SIZE_MAX :
        keepFirst + (size_t)(length * TH_DEFAULT_SAMPLES);
    const auto kept = [&] (size_t at) { return at >= keepFirst &&
                                               at < keepLast; };

    /* By the instrument declared on it, as the tape's header names it,
       whatever a swap put on it since. */
    const auto channelName = [&] (int ch) -> std::string
    {
        const thcInstrument *inst = sched.channelOf(ch);

        return inst != NULL ? inst->name : sched.holding(ch);
    };

    if (!stemsDir.empty())
    {
        std::error_code ec;

        std::filesystem::create_directories(stemsDir, ec);
    }

    pcm.reserve((size_t)((seconds + TAIL_MAX) / dt + 1) * frame);

    /* --profile's: each window's seconds, where in the piece it started
       (-1 in the tail), and each channel's seconds in it, the master
       effect's last. */
    const int profiled = synth.midiChanCount() + 1;
    std::vector<double> windowSeconds, windowAt, channelSeconds;

    if (profile)
        synth.setProfiling(true);

    /* One window of audio per step of the clock. The scheduler enqueues
       what it delivers and process() applies it, exactly as the GUI's
       timer and the audio thread do between them. */
    auto renderWindow = [&](bool transportWindow, double windowStart) -> float
    {
        const auto t0 = std::chrono::steady_clock::now();
        const size_t at0 = windowsRendered * (size_t)window;

        synth.process();

        if (profile)
        {
            windowSeconds.push_back(std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count());
            windowAt.push_back(transportWindow ? windowStart : -1);
            channelSeconds.insert(channelSeconds.end(),
                                  synth.channelSeconds(),
                                  synth.channelSeconds() + profiled);
        }

        const float *buf = synth.getOutput();
        float peak = 0;

        for (size_t i = 0; i < frame; i++)
        {
            const float a = fabsf(buf[i]);

            if (a > peak)
                peak = a;
        }

        if (levels)
            for (int ch = 0; ch < synth.midiChanCount(); ch++)
            {
                int outputs = 0;
                const float *signal = synth.getChannelOutput(ch, &outputs);

                if (signal == NULL)
                    continue;

                if (!channelLoudness[ch])
                    channelLoudness[ch].reset(
                        new Loudness(outputs, TH_DEFAULT_SAMPLES));

                /* Interleaved, so a frame at a time as it stands. */
                for (int i = 0; i < window; i++)
                    if (kept(at0 + i))
                    {
                        for (int k = 0; k < outputs; k++)
                            channelLevels[ch].add(signal[i * outputs + k]);

                        channelLoudness[ch]->add(signal + i * outputs);
                    }
            }

        for (int ch = 0; ch < (int)stems.size(); ch++)
        {
            int outputs = 0;
            const float *signal = synth.getChannelOutput(ch, &outputs);

            if (stems[ch].file == NULL && signal != NULL &&
                !channelName(ch).empty())
            {
                const std::string path = stemsDir + "/" +
                    std::to_string(ch) + "-" + channelName(ch) + ".wav";

                stems[ch].first = keepFirst;
                stems[ch].last = keepLast;

                if (stems[ch].open(path, outputs, TH_DEFAULT_SAMPLES))
                {
                    const std::vector<float> quiet(
                        (size_t)outputs * window, 0.0f);

                    for (size_t w = 0; w < windowsRendered; w++)
                        stems[ch].add(quiet.data(), quiet.size());
                }
                else
                    stems[ch].file = NULL;
            }

            if (stems[ch].file == NULL)
                continue;

            if (signal != NULL && outputs == stems[ch].outputs)
                stems[ch].add(signal, (size_t)outputs * window);
            else
            {
                const std::vector<float> quiet(
                    (size_t)stems[ch].outputs * window, 0.0f);

                stems[ch].add(quiet.data(), quiet.size());
            }
        }

        windowsRendered++;

        if (other)
        {
            otherSynth->process();

            const float *b = otherSynth->getOutput();

            /* The tail runs until both are silent. */
            for (size_t i = 0; i < frame; i++)
            {
                mixDiff.add(buf[i] - b[i]);
                peak = std::max(peak, fabsf(b[i]));
            }

            /* A channel only one of the two renders sounded on is all
               difference, so the silent side reads as zeros. */
            for (int ch = 0; ch < synth.midiChanCount(); ch++)
            {
                int na = 0, nb = 0;
                const float *sa = synth.getChannelOutput(ch, &na);
                const float *sb = otherSynth->getChannelOutput(ch, &nb);

                if (sa == NULL) na = 0;
                if (sb == NULL) nb = 0;

                for (int k = 0; k < std::max(na, nb) * window; k++)
                    channelDiffs[ch].add((k < na * window ? sa[k] : 0) -
                                         (k < nb * window ? sb[k] : 0));
            }
        }

        if (sections && transportWindow)
            for (int i = 0; i < window; i++)
            {
                const int section = sched.sectionAt(
                    windowStart + (i + 0.5) / (double)TH_DEFAULT_SAMPLES);

                if (section >= 0 && kept(at0 + i))
                    for (int ch = 0; ch < channels; ch++)
                        sectionLevels[section].add(buf[ch * window + i]);
            }

        /* getOutput() is planar -- a window of channel 0, then a window of
           channel 1 -- and a WAV is interleaved. Copied as it stood, every
           window came out as channel 0 at double speed across both
           speakers and then channel 1 the same way: an octave up, chopped
           forty-three times a second. The tape was never affected, which
           is why gencheck and compare.mjs had nothing to say about it. */
        for (int i = 0; i < window; i++)
        {
            for (int c = 0; c < channels; c++)
                pcm.push_back(buf[(size_t)c * window + i]);

            if (levels && kept(at0 + i))
                mixLoudness.add(&pcm[pcm.size() - channels]);
        }

        return peak;
    };

    sched.start();

    if (other)
        other->start();

    double marked = -1;

    thcMidiExport::markSection(midi, sched, marked);

    /* Until the time asked for, or until the piece is over: a piece
       whose arrangement closes with `section end;' stops its own
       transport, and a loop that only watched the clock would step a
       stopped scheduler for the rest of the render. */
    size_t nextMove = 0;

    /* Where each section began as it played, for --json: what the piece
       did, whatever its arrangement says it does. A change is seen a
       window late and found between the two windows by halving, so a
       section starts where it does rather than up to a window after. */
    std::vector<std::pair<double, int> > timeline;
    double lastStart = 0;

    while (sched.now() < seconds && sched.running())
    {
        const double windowStart = sched.now();
        const int section = sched.sectionAt(windowStart);

        if (section >= 0 && (timeline.empty() || timeline.back().second !=
                                                     section))
        {
            double lo = lastStart, hi = windowStart;

            for (int k = 0; k < 40 && !timeline.empty(); k++)
            {
                const double mid = (lo + hi) / 2;

                (sched.sectionAt(mid) == section ? hi : lo) = mid;
            }

            timeline.push_back(std::make_pair(timeline.empty() ? windowStart
                                                               : hi,
                                              section));
        }

        lastStart = windowStart;

        for (; nextMove < moves.size() && moves[nextMove].at <= windowStart;
             nextMove++)
        {
            thArg *k = sched.knob(moves[nextMove].name);

            k->setValue(moves[nextMove].value);
            sched.knobMoved(k);
        }

        sched.stepTransport(dt);

        if (other)
            other->stepTransport(dt);

        renderWindow(true, windowStart);
        thcMidiExport::markSection(midi, sched, marked);
    }

    /* stop() flushes the note-offs for whatever is still sounding; the
       releases that follow are part of the piece. */
    const double stoppedAt = sched.now();

    sched.stop();

    if (other)
        other->stop();

    conn.disconnect();
    midi.end(stoppedAt);

    for (double tail = 0; tail < TAIL_MAX; tail += dt)
        if (renderWindow(false, 0) < TAIL_SILENT)
            break;

    if (tape != NULL && tape != stdout)
        fclose(tape);

    for (Stem &stem : stems)
        if (stem.file != NULL && !stem.close())
        {
            fprintf(stderr, "%s: cannot write a stem in %s\n", argv[0],
                    stemsDir.c_str());
            return 1;
        }

    /* A loop is the second pass of a phrase rendered twice: by then the
       first pass's tails are sounding under it, as they will be when it
       wraps round onto itself. scripts/makekit.sh's loops. */
    if (from > 0 || length >= 0)
    {
        from = std::max(from, 0.0);

        const size_t first = std::min(pcm.size(),
            (size_t)(from * TH_DEFAULT_SAMPLES) * (size_t)channels);
        const size_t last = length < 0 ? pcm.size() : std::min(pcm.size(),
            first + (size_t)(length * TH_DEFAULT_SAMPLES) *
                    (size_t)channels);

        pcm = std::vector<float>(pcm.begin() + first, pcm.begin() + last);
    }

    float peak = 0;
    double sumsq = 0;
    size_t clipped = 0;

    for (size_t i = 0; i < pcm.size(); i++)
    {
        const float a = fabsf(pcm[i]);

        if (a > peak)
            peak = a;

        if (a >= 0.999f)
            clipped++;

        sumsq += (double)pcm[i] * pcm[i];
    }

    /* Summed rather than left as a stereo pair, for one caller:
       scripts/makekit.sh, which renders the tree's own drums into
       dsp/samples/ for osc::sample to play. That node is mono -- a voice
       has one output, and a graph that wants two instantiates two nodes
       -- so it sums a stereo file itself on the way in. Doing it here
       instead halves what the repository carries and loses nothing,
       since every drum graph in the tree writes the same signal to both
       sides anyway.

       Averaged, not added: two sides of the same signal have to come out
       at the level they went in. */
    int outChannels = channels;

    if (mono && channels > 1)
    {
        std::vector<float> summed;

        summed.reserve(pcm.size() / (size_t)channels);

        for (size_t i = 0; i + (size_t)channels <= pcm.size();
             i += (size_t)channels)
        {
            double sum = 0;

            for (int c = 0; c < channels; c++)
                sum += pcm[i + (size_t)c];

            summed.push_back((float)(sum / channels));
        }

        pcm.swap(summed);
        outChannels = 1;
    }

    if (!wavFile.empty() &&
        !writeWav(wavFile, pcm, outChannels, TH_DEFAULT_SAMPLES))
    {
        fprintf(stderr, "%s: cannot write %s\n", argv[0], wavFile.c_str());
        return 1;
    }

    if (!midiFile.empty() && !midi.write(midiFile))
    {
        fprintf(stderr, "%s: cannot write %s\n", argv[0], midiFile.c_str());
        return 1;
    }

    if (!midiFile.empty() && !quiet && midi.skipped() > 0)
        fprintf(stderr, "%s: %zu events with no MIDI spelling left out of "
                "%s\n",
                genFile.c_str(), midi.skipped(), midiFile.c_str());

    /* Voices thMidiChan::mixNote dropped for going non-finite. Unreported,
       the render is just quieter than it should be -- or silent, if the piece
       is one diverging instrument -- and the peak and RMS below are an honest
       report of the wrong signal. */
    const unsigned long badVoices = synth.nonFiniteVoices();

    if (!quiet)
    {
        fprintf(stderr, "%s: %.1f s rendered, %zu notes, peak %.3f, "
                "RMS %.4f, %zu clipped sample%s\n",
                genFile.c_str(), pcm.size() / (double)frame * dt, notes,
                peak, pcm.empty() ? 0.0 : sqrt(sumsq / pcm.size()),
                clipped, clipped == 1 ? "" : "s");

        if (badVoices > 0)
            fprintf(stderr, "%s: non-finite voices: %lu\n", genFile.c_str(),
                    badVoices);
    }

    /* Both numbers, because this one command prints both: `channel' is
       the one-based number the application shows, `engine' the
       zero-based one the tape's event lines carry. Naming only one of
       them left the reader to discover the other by being wrong. */
    if (levelsText)
    {
        fprintf(stderr,
                "channel  engine  instrument               peak     RMS     "
                "LUFS%s\n", other ? "  diff RMS" : "");

        for (size_t ch = 0; ch < channelLevels.size(); ch++)
        {
            const Level &level = channelLevels[ch];

            if (!level.count && !channelDiffs[ch].count)
                continue;

            const std::string name = channelName((int)ch);

            fprintf(stderr, "%7zu  %6zu  %-24s %.3f  %.4f  %s", ch + 1, ch,
                    name.empty() ? "-" : name.c_str(), level.peak,
                    level.rms(), lufsText(channelLoudness[ch].get()).c_str());

            if (other)
                fprintf(stderr, "  %.4f", channelDiffs[ch].rms());

            fputc('\n', stderr);
        }

        fprintf(stderr, "%-41s %.3f  %.4f  %s", "mix", peak,
                pcm.empty() ? 0.0 : sqrt(sumsq / pcm.size()),
                lufsText(&mixLoudness).c_str());

        if (other)
            fprintf(stderr, "  %.4f", mixDiff.rms());

        fputc('\n', stderr);
    }

    if (sectionsText)
    {
        fprintf(stderr, "section                   mix RMS\n");

        for (size_t i = 0; i < sectionLevels.size(); i++)
            fprintf(stderr, "%-24s  %.4f\n", sched.sections()[i].name.c_str(),
                    sectionLevels[i].rms());
    }

    std::string profileJson;

    if (profile && !windowSeconds.empty())
    {
        const size_t n = windowSeconds.size();
        const auto ms = [] (double sec) { return sec * 1000; };
        const auto at = [&] (size_t w) -> std::string
        {
            if (windowAt[w] < 0)
                return "tail";

            const double t = windowAt[w];
            const int section = sched.sectionAt(t);
            /* Rounded once, in tenths, so 179.97 s reads 3:00.0 and not
               2:60.0. */
            const long tenths = lround(t * 10);
            char text[128];

            snprintf(text, sizeof text, "%ld:%02ld.%ld%s%s", tenths / 600,
                     tenths % 600 / 10, tenths % 10, section >= 0 ? " " : "",
                     section >= 0 ? sched.sections()[section].name.c_str()
                                  : "");
            return text;
        };
        const auto name = [&] (int ch) -> std::string
        {
            if (ch == profiled - 1)
                return "master effect";

            const std::string held = channelName(ch);

            return held.empty() ? "channel " + std::to_string(ch + 1) : held;
        };
        std::vector<size_t> byCost(n);
        double sum = 0;

        for (size_t w = 0; w < n; w++)
        {
            byCost[w] = w;
            sum += windowSeconds[w];
        }

        std::sort(byCost.begin(), byCost.end(), [&] (size_t a, size_t b)
                  { return windowSeconds[a] > windowSeconds[b]; });

        /* A window over its time now and then is a click; a second over
           on average is a machine that cannot keep up with the piece. */
        const size_t span = std::min(n, std::max<size_t>(1,
                                          (size_t)lround(1.0 / dt)));
        double run = 0, slowest = 0;
        size_t slowestAt = 0;

        for (size_t w = 0; w < n; w++)
        {
            run += windowSeconds[w];

            if (w >= span)
                run -= windowSeconds[w - span];

            if (w + 1 >= span && run > slowest)
            {
                slowest = run;
                slowestAt = w + 1 - span;
            }
        }

        const size_t worst = byCost[0];

        /* For --json: the same numbers, by channel. */
        if (!jsonFile.empty())
        {
            profileJson = "{";
            jsonKey(profileJson, "windowMs");
            jsonNumber(profileJson, ms(dt));
            jsonKey(profileJson, "meanMs");
            jsonNumber(profileJson, ms(sum / n));
            jsonKey(profileJson, "p99Ms");
            jsonNumber(profileJson, ms(windowSeconds[byCost[n / 100]]));
            jsonKey(profileJson, "worstMs");
            jsonNumber(profileJson, ms(windowSeconds[worst]));
            jsonKey(profileJson, "worstAt");
            jsonString(profileJson, at(worst));
            jsonKey(profileJson, "slowestSecondShare");
            jsonNumber(profileJson, slowest / (span * dt));
            jsonKey(profileJson, "slowestSecondAt");
            jsonString(profileJson, at(slowestAt));
            jsonKey(profileJson, "channels");
            profileJson += '[';

            for (int ch = 0; ch < profiled; ch++)
            {
                double total = 0, top = 0;

                for (size_t w = 0; w < n; w++)
                {
                    total += channelSeconds[w * profiled + ch];
                    top = std::max(top, channelSeconds[w * profiled + ch]);
                }

                if (total == 0)
                    continue;

                if (profileJson.back() != '[')
                    profileJson += ',';

                profileJson += '{';
                jsonKey(profileJson, "name");
                jsonString(profileJson, name(ch));
                jsonKey(profileJson, "meanMs");
                jsonNumber(profileJson, ms(total / n));
                jsonKey(profileJson, "worstMs");
                jsonNumber(profileJson, ms(top));
                jsonKey(profileJson, "share");
                jsonNumber(profileJson, total / sum);
                profileJson += '}';
            }

            profileJson += "]}";
        }
        else
        {
            fprintf(stderr, "profile: %zu windows of %d frames, %.2f ms each\n",
                    n, window, ms(dt));
            fprintf(stderr, "window          mean %.2f ms, p99 %.2f ms, worst "
                    "%.2f ms (%.0f%% of its time) at %s\n", ms(sum / n),
                    ms(windowSeconds[byCost[n / 100]]), ms(windowSeconds[worst]),
                    100 * windowSeconds[worst] / dt, at(worst).c_str());
            fprintf(stderr, "slowest second  %.2f ms a window, %.0f%% of real "
                    "time, from %s\n", ms(slowest / span),
                    100 * slowest / (span * dt), at(slowestAt).c_str());
            fprintf(stderr, "worst windows\n");

            for (size_t k = 0; k < std::min<size_t>(8, n); k++)
            {
                const size_t w = byCost[k];
                const double *cost = &channelSeconds[w * profiled];
                std::vector<int> heaviest(profiled);

                for (int ch = 0; ch < profiled; ch++)
                    heaviest[ch] = ch;

                std::sort(heaviest.begin(), heaviest.end(), [&] (int a, int b)
                          { return cost[a] > cost[b]; });
                fprintf(stderr, "  %-28s %7.2f ms ", at(w).c_str(),
                        ms(windowSeconds[w]));

                for (int j = 0; j < 3 && cost[heaviest[j]] > 0; j++)
                    fprintf(stderr, "%s %s %.2f", j ? "," : "",
                            name(heaviest[j]).c_str(), ms(cost[heaviest[j]]));

                fputc('\n', stderr);
            }

            fprintf(stderr, "channel  instrument               mean ms  "
                    "worst ms  share\n");

            for (int ch = 0; ch < profiled; ch++)
            {
                double total = 0, top = 0;

                for (size_t w = 0; w < n; w++)
                {
                    total += channelSeconds[w * profiled + ch];
                    top = std::max(top, channelSeconds[w * profiled + ch]);
                }

                if (total == 0)
                    continue;

                fprintf(stderr, "%7s  %-24s %7.3f  %8.2f  %4.1f%%\n",
                        ch == profiled - 1 ? "-" : std::to_string(ch + 1).c_str(),
                        name(ch).c_str(), ms(total / n), ms(top),
                        100 * total / sum);
            }
    
        }
    }

    if (!jsonFile.empty())
    {
        std::string out = "{";

        jsonKey(out, "ok");
        out += "true";
        jsonKey(out, "errors");
        jsonStrings(out, loader.errors());
        jsonKey(out, "warnings");
        jsonStrings(out, loader.warnings());
        describeJson(out, loader, sched);
        jsonKey(out, "rendered");
        jsonNumber(out, pcm.size() / (double)outChannels / TH_DEFAULT_SAMPLES);
        jsonKey(out, "stopped");
        jsonNumber(out, stoppedAt);
        jsonKey(out, "notes");
        jsonInt(out, (long)notes);
        jsonKey(out, "clipped");
        jsonInt(out, (long)clipped);
        jsonKey(out, "nonFinite");
        jsonInt(out, (long)badVoices);
        jsonKey(out, "mix");
        out += '{';
        jsonKey(out, "peak");
        jsonNumber(out, peak);
        jsonKey(out, "rms");
        jsonNumber(out, pcm.empty() ? 0.0 : sqrt(sumsq / pcm.size()));
        jsonKey(out, "lufs");
        out += lufsText(&mixLoudness) == "-inf" ? "null"
                                               : lufsText(&mixLoudness);

        if (other)
        {
            jsonKey(out, "diffRms");
            jsonNumber(out, mixDiff.rms());
        }

        out += '}';
        jsonKey(out, "channels");
        out += '[';

        for (size_t ch = 0; ch < channelLevels.size(); ch++)
        {
            if (!channelLevels[ch].count && !channelDiffs[ch].count)
                continue;

            const std::string lufs = lufsText(channelLoudness[ch].get());

            if (out.back() != '[')
                out += ',';

            out += '{';
            jsonKey(out, "engine");
            jsonInt(out, (long)ch);
            jsonKey(out, "instrument");
            jsonString(out, channelName((int)ch));
            jsonKey(out, "peak");
            jsonNumber(out, channelLevels[ch].peak);
            jsonKey(out, "rms");
            jsonNumber(out, channelLevels[ch].rms());
            jsonKey(out, "lufs");
            out += lufs == "-inf" ? "null" : lufs;

            if (other)
            {
                jsonKey(out, "diffRms");
                jsonNumber(out, channelDiffs[ch].rms());
            }

            out += '}';
        }

        out += ']';
        /* In the written audio's time: what --from and --length cut away
           is not in it, and the section playing at the cut begins it. One
           that begins within a sample of the end -- a section edge is a
           microsecond early, so a note on it is its own -- is not in it.
           `pieceAt' is where it began in the piece, for bar numbers. */
        const double cut = std::max(from, 0.0);

        jsonKey(out, "from");
        jsonNumber(out, cut);
        jsonKey(out, "timeline");
        out += '[';

        for (size_t k = 0; k < timeline.size(); k++)
        {
            const double at = timeline[k].first - cut;

            if ((k + 1 < timeline.size() && timeline[k + 1].first - cut <= 0) ||
                (length >= 0 && at >= length - 1.0 / TH_DEFAULT_SAMPLES))
                continue;

            if (out.back() != '[')
                out += ',';

            out += '{';
            jsonKey(out, "at");
            jsonNumber(out, std::max(at, 0.0));
            jsonKey(out, "pieceAt");
            jsonNumber(out, timeline[k].first);
            jsonKey(out, "section");
            jsonString(out, sched.sections()[timeline[k].second].name);
            out += '}';
        }

        out += ']';
        jsonKey(out, "sectionRms");
        out += '{';

        for (size_t i = 0; i < sectionLevels.size(); i++)
        {
            jsonKey(out, sched.sections()[i].name.c_str());
            jsonNumber(out, sectionLevels[i].rms());
        }

        out += '}';

        if (!profileJson.empty())
        {
            jsonKey(out, "profile");
            out += profileJson;
        }

        out += '}';

        if (!writeText(jsonFile, out))
        {
            fprintf(stderr, "%s: cannot write %s\n", argv[0],
                    jsonFile.c_str());
            return 1;
        }
    }

    /* 4 before 3: a piece that clips is loud, a piece with a non-finite
       voice in it is not the piece. */
    if (badVoices > 0)
        return 4;

    return clipped > 0 ? 3 : 0;
}
