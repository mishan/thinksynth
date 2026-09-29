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
 * plugincheck -- the built CLAP plays what the engine plays.
 *
 *   plugincheck build/bin/thinksynth-juno.clap
 *
 * A host of its own, as small as a CLAP host can be: it dlopens the
 * plugin, creates it, lists its parameters, and plays a phrase through it
 * in blocks of uneven sizes -- 100 frames, then 37, then 256, then 1, and
 * so on, as no audio device would but as a DAW's automation splits can --
 * with a note on, a parameter change and a note off at frames of their
 * own. Beside it a thSynth plays the same .dsp with the same events at
 * the same windows, and the two have to agree bit for bit. At 48 kHz and
 * again at 44.1, which is where a control given in ms has to be folded at
 * the host's rate and not the one its range was read at.
 *
 * What that holds down: the window adapter in ThinkPlugin::run (a window
 * handed out across blocks, an event applied before the window its frame
 * falls in), the parameters reaching the graph folded at the right rate,
 * and the plugin carrying the graph it was built from. And the parameter
 * list itself: the .dsp's controls in its order, its labels and ranges,
 * ms as ms, and the level last.
 *
 * Headless and silent: nothing here opens a device. Exit status is the
 * number of failures.
 */

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "clap/entry.h"
#include "clap/events.h"
#include "clap/ext/params.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include "clap/plugin.h"
#include "clap/process.h"

#include "think.h"

#include "thinksynth_dsp.h"

static int failed = 0;

static void check (bool ok, const std::string &what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());

    if (!ok)
        failed++;
}

/* ---- the host's side of CLAP ------------------------------------------ */

static const void *hostExtension (const clap_host_t *, const char *)
{
    return NULL;
}

static void hostRequest (const clap_host_t *) { }

static const clap_host_t host = {
    CLAP_VERSION, NULL, "plugincheck", "thinksynth", "", "0",
    hostExtension, hostRequest, hostRequest, hostRequest,
};

/* One block's events, in time order. */
struct Events
{
    std::vector<clap_event_midi_t> midi;
    std::vector<clap_event_param_value_t> params;
    std::vector<const clap_event_header_t *> list;

    void clear (void)
    {
        midi.clear();
        params.clear();
        list.clear();
    }

    /* After every push, since the vectors may move. */
    void index (void)
    {
        list.clear();

        size_t m = 0, p = 0;

        while (m < midi.size() || p < params.size())
        {
            if (p >= params.size() ||
                (m < midi.size() && midi[m].header.time <= params[p].header.time))
                list.push_back(&midi[m++].header);
            else
                list.push_back(&params[p++].header);
        }
    }
};

static uint32_t eventsSize (const clap_input_events_t *in)
{
    return (uint32_t)((const Events *)in->ctx)->list.size();
}

static const clap_event_header_t *eventsGet (const clap_input_events_t *in,
                                             uint32_t i)
{
    return ((const Events *)in->ctx)->list[i];
}

static bool eventsPush (const clap_output_events_t *,
                        const clap_event_header_t *)
{
    return true;
}

/* ---- the phrase ------------------------------------------------------- */

const int kWindow = 64;           /* ThinkPlugin's */
const int kWindows = 40;
const int kNoteOnWindow = 0;
const int kParamWindow = 8;
const int kNoteOffWindow = 20;
const float kCutoff = 2400.0f;

/* What happens at an absolute frame. */
struct Cue
{
    uint32_t frame;
    bool param;
    uint8_t midi[3];
};

static std::vector<Cue> phrase (void)
{
    std::vector<Cue> cues;
    Cue c;

    c.param = false;
    c.frame = kNoteOnWindow * kWindow;
    c.midi[0] = 0x90; c.midi[1] = 60; c.midi[2] = 100;
    cues.push_back(c);

    c.param = true;
    c.frame = kParamWindow * kWindow;
    cues.push_back(c);

    c.param = false;
    c.frame = kNoteOffWindow * kWindow;
    c.midi[0] = 0x80; c.midi[1] = 60; c.midi[2] = 0;
    cues.push_back(c);

    return cues;
}

/* ---- the plugin's render ---------------------------------------------- */

struct Rendered
{
    std::vector<float> left, right;
};

static bool viaPlugin (const clap_plugin_factory_t *factory, const char *id,
                       double rate, clap_id cutoffId, Rendered &out)
{
    const clap_plugin_t *plugin = factory->create_plugin(factory, &host, id);

    if (plugin == NULL || !plugin->init(plugin) ||
        !plugin->activate(plugin, rate, 1, 4096) ||
        !plugin->start_processing(plugin))
        return false;

    static const uint32_t sizes[] = { 100, 37, 256, 1, 64, 500, 3, 127, 800 };
    const uint32_t total = kWindows * kWindow;
    const std::vector<Cue> cues = phrase();

    std::vector<float> left(1024), right(1024);
    float *channels[2];
    Events events;
    clap_input_events_t in = { &events, eventsSize, eventsGet };
    clap_output_events_t outEvents = { NULL, eventsPush };

    for (uint32_t pos = 0, s = 0; pos < total; s++)
    {
        uint32_t n = sizes[s % (sizeof(sizes) / sizeof(sizes[0]))];

        if (n > total - pos)
            n = total - pos;

        events.clear();

        for (size_t i = 0; i < cues.size(); i++)
        {
            if (cues[i].frame < pos || cues[i].frame >= pos + n)
                continue;

            if (cues[i].param)
            {
                clap_event_param_value_t e;

                memset(&e, 0, sizeof(e));
                e.header.size = sizeof(e);
                e.header.time = cues[i].frame - pos;
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.header.type = CLAP_EVENT_PARAM_VALUE;
                e.param_id = cutoffId;
                e.note_id = -1;
                e.port_index = -1;
                e.channel = -1;
                e.key = -1;
                e.value = kCutoff;
                events.params.push_back(e);
            }
            else
            {
                clap_event_midi_t e;

                memset(&e, 0, sizeof(e));
                e.header.size = sizeof(e);
                e.header.time = cues[i].frame - pos;
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.header.type = CLAP_EVENT_MIDI;
                e.port_index = 0;
                memcpy(e.data, cues[i].midi, 3);
                events.midi.push_back(e);
            }
        }

        events.index();

        channels[0] = &left[0];
        channels[1] = &right[0];

        clap_audio_buffer_t buffer;

        memset(&buffer, 0, sizeof(buffer));
        buffer.data32 = channels;
        buffer.channel_count = 2;

        clap_process_t process;

        memset(&process, 0, sizeof(process));
        process.steady_time = pos;
        process.frames_count = n;
        process.audio_outputs = &buffer;
        process.audio_outputs_count = 1;
        process.in_events = &in;
        process.out_events = &outEvents;

        if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR)
            return false;

        out.left.insert(out.left.end(), left.begin(), left.begin() + n);
        out.right.insert(out.right.end(), right.begin(), right.begin() + n);

        pos += n;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    return true;
}

/* ---- the engine's ----------------------------------------------------- */

static bool viaEngine (double rate, Rendered &out)
{
    thSynth synth("", kWindow, (int)rate);

    if (synth.loadTreeText(thPluginDspName, thPluginDspText, 0,
                           TH_DEFAULT_CHAN_AMP) == NULL)
        return false;

    const int channels = synth.audioChannelCount();

    for (int w = 0; w < kWindows; w++)
    {
        if (w == kNoteOnWindow)
            synth.addNote(0, 60, 100);
        else if (w == kParamWindow)
            synth.getChanArg(0, "cutoff")->setValue(kCutoff);
        else if (w == kNoteOffWindow)
            synth.delNote(0, 60);

        synth.process();

        const float *buf = synth.getOutput();

        out.left.insert(out.left.end(), buf, buf + kWindow);
        out.right.insert(out.right.end(), buf + (channels > 1 ? kWindow : 0),
                         buf + (channels > 1 ? 2 * kWindow : kWindow));
    }

    return true;
}

static bool identical (const std::vector<float> &a, const std::vector<float> &b,
                       size_t &firstBad)
{
    if (a.size() != b.size())
    {
        firstBad = 0;
        return false;
    }

    for (size_t i = 0; i < a.size(); i++)
        if (memcmp(&a[i], &b[i], sizeof(float)) != 0)
        {
            firstBad = i;
            return false;
        }

    return true;
}

static double peak (const std::vector<float> &v)
{
    double p = 0;

    for (size_t i = 0; i < v.size(); i++)
        if (fabs(v[i]) > p)
            p = fabs(v[i]);

    return p;
}

int main (int argc, char **argv)
{
    if (argc != 2)
    {
        printf("usage: %s PLUGIN.clap\n", argv[0]);
        return 2;
    }

    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);

    if (module == NULL)
    {
        printf("FAIL  %s\n", dlerror());
        return 2;
    }

    const clap_plugin_entry_t *entry =
        (const clap_plugin_entry_t *)dlsym(module, "clap_entry");

    if (entry == NULL || !entry->init(argv[1]))
    {
        printf("FAIL  no clap_entry, or it would not initialise\n");
        return 2;
    }

    const clap_plugin_factory_t *factory = (const clap_plugin_factory_t *)
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID);

    check(factory != NULL && factory->get_plugin_count(factory) == 1,
          "one plugin in the factory");

    if (factory == NULL)
        return failed;

    const clap_plugin_descriptor_t *desc =
        factory->get_plugin_descriptor(factory, 0);

    check(desc != NULL && !strcmp(desc->id, "org.thinksynth.juno"),
          std::string("its id is org.thinksynth.juno: ") +
          (desc ? desc->id : "(none)"));

    /* ---- the parameters ---- */

    clap_id cutoffId = CLAP_INVALID_ID;

    {
        const clap_plugin_t *plugin =
            factory->create_plugin(factory, &host, desc->id);

        plugin->init(plugin);

        const clap_plugin_params_t *params = (const clap_plugin_params_t *)
            plugin->get_extension(plugin, CLAP_EXT_PARAMS);

        const uint32_t n = params ? params->count(plugin) : 0;
        std::vector<std::string> names;
        clap_param_info_t fa, level;

        memset(&fa, 0, sizeof(fa));
        memset(&level, 0, sizeof(level));

        for (uint32_t i = 0; i < n; i++)
        {
            clap_param_info_t info;

            params->get_info(plugin, i, &info);
            names.push_back(info.name);

            if (!strcmp(info.name, "Cutoff (Hz)"))
                cutoffId = info.id;
            if (!strcmp(info.name, "Filter Attack"))
                fa = info;
            if (i == n - 1)
                level = info;
        }

        check(n == 17, "17 parameters, juno's 16 controls and the level: " +
                       std::to_string(n));
        check(!names.empty() && names[0] == "Pulse Width",
              "in the order juno.dsp declares them: Pulse Width first");
        check(!strcmp(level.name, "Level") && level.default_value == 30 &&
              level.max_value == 127,
              "the level last, 0..127 at 30");
        check(fa.default_value == 180 && fa.min_value == 0 &&
              fa.max_value == 3000,
              "Filter Attack is in ms: 180, 0..3000");
        check(cutoffId != CLAP_INVALID_ID, "Cutoff (Hz) is there");

        plugin->destroy(plugin);
    }

    /* ---- the phrase, through both, at two rates ---- */

    const double rates[] = { 48000, 44100 };

    for (int r = 0; r < 2; r++)
    {
        Rendered plugin, engine;
        char what[128];

        const bool ran = viaPlugin(factory, desc->id, rates[r], cutoffId,
                                   plugin) && viaEngine(rates[r], engine);

        size_t bad = 0;
        const bool same = ran && identical(plugin.left, engine.left, bad) &&
                          identical(plugin.right, engine.right, bad);

        snprintf(what, sizeof(what),
                 "at %g Hz the plugin plays what the engine plays, bit for "
                 "bit (%s)", rates[r],
                 !ran ? "did not run"
                      : same ? "peak " : ("first difference at frame " +
                                          std::to_string(bad)).c_str());

        std::string line = what;

        if (same)
        {
            char p[32];

            snprintf(p, sizeof(p), "%.3f", peak(engine.left));
            line = line.substr(0, line.size() - 1) + p + ")";
        }

        check(same, line);
        check(ran && peak(plugin.left) > 0.01, "and it is not silence");
    }

    entry->deinit();
    dlclose(module);

    printf("\n%d failure(s)\n", failed);

    return failed;
}
