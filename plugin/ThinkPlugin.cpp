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
 * thinksynth as an audio plugin: one .dsp, compiled in, played as an
 * instrument by whatever host loads it -- CLAP, VST3 or LV2, through DPF.
 *
 * The engine is think_embedded, libthink with every DSP plugin linked in,
 * so the plugin needs no files beside it. The graph is the .dsp's text,
 * embedded at build time (thinksynth_dsp.h, written by CMakeLists.txt) and
 * loaded with thSynth::loadTreeText.
 *
 * PARAMETERS are the graph's controls: every @chanarg it declares with a
 * widget, in the order it declares them, and then the channel's level.
 * A host keys a parameter by its index here, in VST3 and CLAP both -- DPF
 * has no stable id of its own yet -- so reordering the controls in the
 * .dsp moves saved automation onto the wrong knob. Adding one at the end
 * does not.
 *
 * A control carries the author's units, not the engine's. `@fa = 180 ms'
 * is 8640 samples to a synth at 48 kHz and 7938 at 44.1, and a host saves
 * a parameter's value in a project: saving samples would change what the
 * project means when it is opened at another rate. So a control whose
 * units fold (ms, %) is offered in them, and folded at this synth's rate
 * when it is set. Ranges are the .dsp's, and a host holds a parameter to
 * its range.
 *
 * THE RATE is the host's, fixed for the synth's life
 * (thSynth::getSampleRate), so the synth is made when the plugin is and
 * made again when the host changes the rate -- which a host only does with
 * processing stopped. The parameter values are the plugin's and outlive
 * it.
 *
 * TIMING. The synth renders windows of kWindow frames and a host asks for
 * blocks of any size, so a window is rendered when the last one runs out
 * and handed out across as many blocks as it takes; gthSynthSource does
 * the same for the desktop and tw_render for the browser. A MIDI event is
 * applied before the window its frame falls in, so it lands up to a
 * window early -- 1.3 ms at 48 kHz -- and never late.
 */

#include "DistrhoPlugin.hpp"

#include <string.h>

#include <string>
#include <vector>

#include "think.h"
#include "thLexer.h"
#include "thUnits.h"

#include "thinksynth_dsp.h"

START_NAMESPACE_DISTRHO

namespace {

/* Frames per synth window. 64 keeps a note within 1.3 ms of its frame at
   48 kHz; the engine's cost per window is small against that. */
const int kWindow = 64;

/* The rate the controls are read at. Any rate would do: every folded value
   is unfolded again at the same rate, which is exact. */
const long kReadRate = 48000;

/* One parameter as the host sees it. */
struct Control
{
    std::string name;       /* the chanarg, or "amp" for the level */
    std::string label;
    std::string units;      /* as the .dsp wrote them */
    float min, max, def;    /* in those units */
    float step;
    std::vector<std::string> valueNames;
};

/* The graph's controls, read off a synth that loads it once. */
std::vector<Control> readControls (void)
{
    std::vector<Control> out;
    thSynth synth("", kWindow, kReadRate);

    if (synth.loadTreeText(thPluginDspName, thPluginDspText, 0,
                           TH_DEFAULT_CHAN_AMP) == NULL)
        return out;

    const thArgMap args = synth.getChanArgs(0);

    /* The order the .dsp first names each control in -- its declarations,
       which come before any node reads one. The map is sorted by name. */
    std::vector<thLexToken> tokens;
    std::vector<std::string> order;

    thLexString(thPluginDspText, tokens);

    for (size_t i = 0; i + 1 < tokens.size(); i++)
    {
        if (tokens[i].kind != thLexToken::PUNCT || tokens[i].text != "@" ||
            tokens[i + 1].kind != thLexToken::WORD)
            continue;

        const std::string &name = tokens[i + 1].text;
        bool seen = false;

        for (size_t k = 0; k < order.size() && !seen; k++)
            seen = order[k] == name;

        if (!seen)
            order.push_back(name);
    }

    order.push_back("amp");

    for (size_t k = 0; k < order.size(); k++)
    {
        thArgMap::const_iterator i = args.find(order[k]);

        if (i == args.end() || i->second == NULL)
            continue;

        thArg *arg = i->second;

        if (arg->widgetType() == thArg::HIDE ||
            arg->type() != thArg::ARG_VALUE || arg->len() != 1)
            continue;

        Control c;
        const std::string &units = arg->units();

        c.name = order[k];
        c.label = arg->label().empty() ? c.name : arg->label();
        c.units = units;
        c.min = (float)thUnfoldUnit(arg->min(), units, kReadRate);
        c.max = (float)thUnfoldUnit(arg->max(), units, kReadRate);
        c.def = (float)thUnfoldUnit((*arg)[0], units, kReadRate);
        c.step = arg->step();
        c.valueNames = arg->valueNames();

        /* The level is the channel's, and its label the desktop's. */
        if (c.name == "amp")
            c.label = "Level";

        out.push_back(c);
    }

    return out;
}

const std::vector<Control> &controls (void)
{
    static const std::vector<Control> c = readControls();

    return c;
}

} /* namespace */

class ThinkPlugin : public Plugin
{
public:
    ThinkPlugin (void)
        : Plugin((uint32_t)controls().size(), 0, 0),
          synth_(NULL), held_(0), pos_(0)
    {
        values_.resize(controls().size());

        for (size_t i = 0; i < values_.size(); i++)
            values_[i] = controls()[i].def;

        window_[0].assign(kWindow, 0.0f);
        window_[1].assign(kWindow, 0.0f);

        build(getSampleRate());
    }

    ~ThinkPlugin (void) override
    {
        delete synth_;
    }

protected:
    const char *getLabel (void) const override
    {
        return thPluginDspLabel;
    }

    const char *getDescription (void) const override
    {
        return thPluginDspDescription;
    }

    const char *getMaker (void) const override
    {
        return "Metaphonic Labs";
    }

    const char *getHomePage (void) const override
    {
        return "https://github.com/mishan/thinksynth";
    }

    const char *getLicense (void) const override
    {
        return "GPL-2.0-or-later";
    }

    uint32_t getVersion (void) const override
    {
        return d_version(0, 1, 0);
    }

    int64_t getUniqueId (void) const override
    {
        return d_cconst('T', 's', 'J', 'u');
    }

    void initParameter (uint32_t index, Parameter &parameter) override
    {
        const Control &c = controls()[index];

        parameter.hints = kParameterIsAutomatable;

        if (c.step == 1.0f)
            parameter.hints |= kParameterIsInteger;

        parameter.name = c.label.c_str();
        parameter.symbol = c.name.c_str();
        parameter.unit = c.units.c_str();
        parameter.ranges.min = c.min;
        parameter.ranges.max = c.max;
        parameter.ranges.def = c.def;

        if (!c.valueNames.empty())
        {
            const uint32_t n = (uint32_t)c.valueNames.size();
            ParameterEnumerationValue *v = new ParameterEnumerationValue[n];

            for (uint32_t k = 0; k < n; k++)
            {
                v[k].value = c.min + (float)k;
                v[k].label = c.valueNames[k].c_str();
            }

            parameter.hints |= kParameterIsInteger;
            parameter.enumValues.count = n;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = v;
        }
    }

    float getParameterValue (uint32_t index) const override
    {
        return values_[index];
    }

    void setParameterValue (uint32_t index, float value) override
    {
        values_[index] = value;
        apply(index);
    }

    /* A host changes the rate only with processing stopped. */
    void sampleRateChanged (double rate) override
    {
        build(rate);
    }

    void activate (void) override
    {
        if (synth_ == NULL || synth_->getSampleRate() != (long)getSampleRate())
            build(getSampleRate());

        held_ = 0;
        pos_ = 0;
    }

    void run (const float **, float **outputs, uint32_t frames,
              const MidiEvent *events, uint32_t eventCount) override
    {
        if (synth_ == NULL)
        {
            memset(outputs[0], 0, frames * sizeof(float));
            memset(outputs[1], 0, frames * sizeof(float));
            return;
        }

        uint32_t done = 0, next = 0;

        while (done < frames)
        {
            if (held_ == 0)
            {
                /* Everything due before this window ends. */
                while (next < eventCount &&
                       events[next].frame < done + (uint32_t)kWindow)
                    midi(events[next++]);

                render();
            }

            const uint32_t n = (frames - done < held_) ? frames - done
                                                       : held_;

            memcpy(outputs[0] + done, &window_[0][pos_], n * sizeof(float));
            memcpy(outputs[1] + done, &window_[1][pos_], n * sizeof(float));

            done += n;
            pos_ += n;
            held_ -= n;
        }

        /* What falls in the part of the block the held window covered goes
           to the next window, which is the earliest it can sound. */
        while (next < eventCount)
            midi(events[next++]);
    }

private:
    /* A synth at `rate' with the graph on channel 0 and every parameter
       as the plugin holds it. */
    void build (double rate)
    {
        delete synth_;

        synth_ = new thSynth("", kWindow, (int)(rate > 0 ? rate : kReadRate));

        if (synth_->loadTreeText(thPluginDspName, thPluginDspText, 0,
                                 TH_DEFAULT_CHAN_AMP) == NULL)
        {
            delete synth_;
            synth_ = NULL;
            return;
        }

        for (uint32_t i = 0; i < values_.size(); i++)
            apply(i);

        held_ = 0;
        pos_ = 0;
    }

    /* A parameter's value into the graph, folded at the synth's rate. A
       scalar chanarg is set in place, which does not allocate and is what
       the browser does from its audio thread. */
    void apply (uint32_t index)
    {
        if (synth_ == NULL)
            return;

        const Control &c = controls()[index];
        thArg *arg = synth_->getChanArg(0, c.name);

        if (arg != NULL && arg->type() == thArg::ARG_VALUE && arg->len() == 1)
            arg->setValue((float)thFoldUnit(values_[index], c.units,
                                            synth_->getSampleRate()));
    }

    /* One window, planar out of the synth into the two held channels. */
    void render (void)
    {
        synth_->process();

        const float *out = synth_->getOutput();
        const int channels = synth_->audioChannelCount();

        memcpy(&window_[0][0], out, kWindow * sizeof(float));
        memcpy(&window_[1][0], out + (channels > 1 ? kWindow : 0),
               kWindow * sizeof(float));

        held_ = kWindow;
        pos_ = 0;
    }

    /* Every MIDI channel plays the one synth channel. */
    void midi (const MidiEvent &ev)
    {
        const uint8_t *d = ev.size > MidiEvent::kDataSize ? ev.dataExt
                                                          : ev.data;

        if (ev.size < 3)
            return;

        switch (d[0] & 0xf0)
        {
        case 0x90:
            if (d[2] > 0)
            {
                synth_->addNote(0, d[1], d[2]);
                break;
            }
            /* velocity 0 is a note-off */
            /* fall through */
        case 0x80:
            synth_->delNote(0, d[1]);
            break;

        case 0xb0:
            synth_->handleMidiController(0, d[1], d[2]);
            break;
        }
    }

    thSynth *synth_;
    std::vector<float> values_;
    std::vector<float> window_[2];
    uint32_t held_;
    uint32_t pos_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThinkPlugin)
};

Plugin *createPlugin (void)
{
    return new ThinkPlugin();
}

END_NAMESPACE_DISTRHO
