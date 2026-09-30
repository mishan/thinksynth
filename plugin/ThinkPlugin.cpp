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
 * TIMING. The synth renders windows of kWindow frames and applies an
 * event only at a window's start, so an event is applied at the start of
 * the window its frame falls in -- which it can only be once the host has
 * sent everything up to that window's end. So a window is rendered when
 * the host reaches its end and heard from there: one window late, a
 * latency the plugin reports and a host compensates. The output is the
 * engine's, applying each event at its window, delayed by exactly
 * kWindow frames (plugincheck holds it to that bit for bit). A MIDI event
 * carries its frame. A parameter change does not: DPF sets it before
 * run() with no frame, so it is timed to the start of the block it
 * arrived with. Anything in the part of a block past the last window
 * rendered waits, stamped, in pending_ for the block that renders its
 * window.
 *
 * Every MIDI channel plays the one synth channel, a key counted across
 * channels. All notes off (CC 123) releases what is held; all sound off
 * (CC 120) cuts it too; and what was sounding when the host deactivated
 * is gone when it activates again.
 */

#include "DistrhoPlugin.hpp"

#include <string.h>

#include <string>
#include <vector>

#include "think.h"
#include "thLexer.h"
#include "thUnits.h"

#include "thinksynth_dsp.h"

#include "Controls.h"

START_NAMESPACE_DISTRHO


class ThinkPlugin : public Plugin
{
public:
    ThinkPlugin (void)
        : Plugin((uint32_t)controls().size(), 0, 0),
          synth_(NULL), held_(0), pos_(0), now_(0), rendered_(0),
          active_(false), pendingHead_(0), pendingCount_(0)
    {
        values_.resize(controls().size());
        dirty_.assign(controls().size(), 0);

        /* One window: every event is applied at the start of the window
           it falls in, which needs the whole window to have arrived. A
           constant, so set here, where a host asks before it activates. */
        setLatency(kWindow);

        for (size_t i = 0; i < values_.size(); i++)
            values_[i] = controls()[i].def;

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

        /* So a host's own control turns it the way the editor's does. */
        if (logScale(c))
            parameter.hints |= kParameterIsLogarithmic;

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
        if (!(value == value))
            return;             /* NaN: nothing to set */

        values_[index] = clamp(index, value);

        /* Between blocks, while processing: timed to the next block's
           start, which is the nearest DPF gives. Otherwise now. */
        if (active_)
            dirty_[index] = 1;
        else
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
        else
        {
            /* Whatever was sounding when the host stopped does not come
               back when it starts again. */
            synth_->clearAll();
            synth_->process();
        }

        start();
        active_ = true;
    }

    void deactivate (void) override
    {
        active_ = false;
    }

    void run (const float **, float **outputs, uint32_t frames,
              const MidiEvent *events, uint32_t eventCount) override
    {
        if (synth_ == NULL)
        {
            memset(outputs[0], 0, frames * sizeof(float));
            memset(outputs[1], 0, frames * sizeof(float));
            now_ += frames;
            return;
        }

        /* This block's parameter changes and MIDI, stamped with the
           absolute frame each is for: a change at the block's start,
           since DPF sets them before run() without a frame, and MIDI at
           its own. They wait in pending_ for the window they fall in. */
        for (uint32_t i = 0; i < dirty_.size(); i++)
            if (dirty_[i])
            {
                dirty_[i] = 0;
                queue(now_, (int)i, NULL);
            }

        for (uint32_t i = 0; i < eventCount; i++)
            queue(now_ + events[i].frame, -1, &events[i]);

        for (uint32_t done = 0; done < frames; )
        {
            /* The host has reached the end of window `rendered_' -- every
               event in it has arrived -- so it can be rendered now, to be
               heard from here on: one window late, which is the latency
               the plugin reports. */
            if (held_ == 0)
            {
                const uint64_t end = (rendered_ + 1) * (uint64_t)kWindow;

                applyBefore(end);
                render();
                rendered_++;
            }

            const uint32_t n = (frames - done < held_) ? frames - done
                                                       : held_;

            memcpy(outputs[0] + done, &window_[0][pos_], n * sizeof(float));
            memcpy(outputs[1] + done, &window_[1][pos_], n * sizeof(float));

            done += n;
            pos_ += n;
            held_ -= n;
            now_ += n;
        }
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

        start();
    }

    /* Processing from the top: the first window out is silence, the
       latency, and window 0 is rendered when the host reaches frame
       kWindow. */
    void start (void)
    {
        window_[0].assign(kWindow, 0.0f);
        window_[1].assign(kWindow, 0.0f);
        held_ = kWindow;
        pos_ = 0;
        now_ = 0;
        rendered_ = 0;
        pendingCount_ = 0;
        pendingHead_ = 0;

        for (int k = 0; k < 128; k++)
            down_[k] = 0;

        for (size_t i = 0; i < dirty_.size(); i++)
            dirty_[i] = 0;
    }

    /* A value inside its control's range. A host keeps to the range it was
       given, but an LV2 host need not. */
    float clamp (uint32_t index, float value) const
    {
        const Control &c = controls()[index];

        return value < c.min ? c.min : value > c.max ? c.max : value;
    }

    /* Onto the end of pending_, in arrival order, which is time order: a
       block's parameter changes are stamped with its start and come first,
       and a host hands over MIDI sorted. A full queue drops the event --
       it holds more than a window of anything a host sends. */
    void queue (uint64_t at, int param, const MidiEvent *ev)
    {
        if (pendingCount_ == kPending)
            return;

        Pending &p = pending_[(pendingHead_ + pendingCount_) % kPending];

        p.at = at;
        p.param = param;

        if (ev != NULL)
        {
            const uint8_t *d = ev->size > MidiEvent::kDataSize ? ev->dataExt
                                                               : ev->data;

            p.size = ev->size < 3 ? ev->size : 3;
            memcpy(p.data, d, p.size);
        }

        pendingCount_++;
    }

    /* Everything pending that falls before `end'. */
    void applyBefore (uint64_t end)
    {
        while (pendingCount_ > 0 && pending_[pendingHead_].at < end)
        {
            const Pending &p = pending_[pendingHead_];

            if (p.param >= 0)
                apply((uint32_t)p.param);
            else
                midi(p.data, p.size);

            pendingHead_ = (pendingHead_ + 1) % kPending;
            pendingCount_--;
        }
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

    /* Every MIDI channel plays the one synth channel. A key is counted
       across channels, so the same key held on two of them is released
       when the last lets go and not the first. */
    void midi (const uint8_t *d, uint32_t size)
    {
        if (size < 3)
            return;

        const uint8_t key = d[1] & 0x7f;

        switch (d[0] & 0xf0)
        {
        case 0x90:
            if (d[2] > 0)
            {
                if (down_[key] < 255)
                    down_[key]++;

                synth_->addNote(0, key, d[2]);
                break;
            }
            /* velocity 0 is a note-off */
            /* fall through */
        case 0x80:
            if (down_[key] > 0 && --down_[key] == 0)
                synth_->delNote(0, key);
            break;

        case 0xb0:
            if (d[1] == 120)            /* all sound off: a cut */
            {
                releaseAll();
                synth_->clearAll();
            }
            else if (d[1] == 123)       /* all notes off: a release */
                releaseAll();
            else
                synth_->handleMidiController(0, d[1], d[2]);
            break;
        }
    }

    void releaseAll (void)
    {
        for (int k = 0; k < 128; k++)
            if (down_[k] > 0)
            {
                down_[k] = 0;
                synth_->delNote(0, k);
            }
    }

    /* One queued thing: a parameter's index, or MIDI bytes. */
    struct Pending
    {
        uint64_t at;
        int param;          /* -1 for MIDI */
        uint8_t data[3];
        uint32_t size;
    };

    static const uint32_t kPending = 1024;

    thSynth *synth_;
    std::vector<float> values_;
    std::vector<char> dirty_;
    std::vector<float> window_[2];
    uint32_t held_;
    uint32_t pos_;
    uint64_t now_;          /* frames handed to the host since start() */
    uint64_t rendered_;     /* windows rendered since start() */
    bool active_;
    Pending pending_[kPending];
    uint32_t pendingHead_;
    uint32_t pendingCount_;
    uint8_t down_[128];     /* note-ons outstanding per key */

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThinkPlugin)
};

Plugin *createPlugin (void)
{
    return new ThinkPlugin();
}

END_NAMESPACE_DISTRHO
