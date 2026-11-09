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
 * VideoRender: genwav's video synth (thVideoSynth), stepped beside its audio
 * synth, hearing it through a thVideoBridge, and written to a .y4m.
 *
 * Its .dsp is loaded on each audio channel it follows and plays the notes
 * the scheduler hands that channel (a thcNoteTap). After each audio window
 * the bridge's measures, and each of the piece's knobs as @knob_<name>, go
 * onto those channels as chanargs (docs/VIDEO.md). A window is no longer
 * than an audio window, so the video trails the audio by less than one,
 * and a note reaches it within two of its time -- unless one line takes
 * longer to scan than an audio window lasts, as on a picture a few lines
 * tall, when a window is that line.
 */

#ifndef VIDEO_RENDER_H
#define VIDEO_RENDER_H

#include <limits.h>
#include <stdio.h>
#include <stdint.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "think.h"
#include "thArg.h"
#include "thSynth.h"
#include "thUtil.h"
#include "thVideoBridge.h"
#include "thVideoFile.h"
#include "thVideoSynth.h"
#include "thcScheduler.h"

class VideoRender : public thcNoteTap
{
public:
    /* `raster' is WxH@FPS; `fx', if not empty, an effect for each channel.
       False, with a sentence, for a raster that is no picture, a .dsp that
       does not load, or a file that cannot be made. */
    bool open (const std::string &pluginPath, const std::string &dsp,
               const std::string &fx, const std::string &raster,
               const std::set<int> &follow, const std::string &path,
               std::string &why)
    {
        int width = 0, height = 0, fps = 0, end = 0;

        if (sscanf(raster.c_str(), "%dx%d@%d%n", &width, &height, &fps, &end) != 3 ||
            end != (int)raster.size() || !video_.open(pluginPath, width, height, fps, 0, why))
        {
            why = "--raster wants WIDTHxHEIGHT@FPS in whole numbers, at most " +
                  std::to_string(TH_MAX_WINDOW_LENGTH) + " wide and " +
                  std::to_string(INT_MAX) + " pixels a second: " + raster;
            return false;
        }

        const std::string found =
            thUtil::findDataFile(dsp, "dsp", "THINK_DSP_PATH", DSP_PATH);
        const std::string foundFx = fx.empty() ? "" :
            thUtil::findDataFile(fx, "dsp", "THINK_DSP_PATH", DSP_PATH);

        for (int ch : follow)
            if (!video_.follow(ch, found.empty() ? dsp : found,
                               foundFx.empty() ? fx : foundFx, why))
                return false;
        return file_.open(path, width, height, fps, why);
    }

    int fps (void) const { return video_.fps(); }

    /* Only frames first..last-1 are written: --from and --length. */
    void keep (int64_t first, int64_t last) { keepFirst_ = first; keepLast_ = last; }

    thSynth *synth (void) { return video_.synth(); }

    /* What the audio synth's last window said, onto every channel this
       follows. */
    void bridge (thSynth &audio, const std::map<std::string, thArg *> &knobs)
    {
        thVideoInputs in;

        bridge_.hear(audio);
        bridge_.inputs(in);
        video_.setInputs(in);
        for (const auto &k : knobs)
            video_.set("knob_" + k.first, (*k.second)[0]);
    }

    /* Every whole window that ends at or before `audio' samples at
       `audioRate'. */
    bool advance (int64_t audio, int64_t audioRate, std::string &why)
    {
        const bool ok = video_.advance(audio, audioRate, write(why));

        said();
        return ok;
    }

    /* On to the end of the frame that holds the last audio sample, and the
       file closed. */
    bool finish (int64_t audio, int64_t audioRate, std::string &why)
    {
        keepLast_ = std::min(keepLast_, (audio * fps() + audioRate - 1) / audioRate);
        const bool ok = video_.finish(audio, audioRate, write(why));

        said();
        return ok && file_.close(why);
    }

    int64_t framesWritten (void) const { return written_; }

    void noteOn (int channel, int note, int velocity, float level,
                 const float *aux) override
    {
        video_.noteOn(channel, note, velocity, level, aux);
    }

    void noteOff (int channel, int note) override { video_.noteOff(channel, note); }
    void flush (int channel) override { video_.flush(channel); }

private:
    thVideoSynth::Frame write (std::string &why)
    {
        return [this, &why](int64_t frame, const float *r, const float *g, const float *b)
        {
            if (frame < keepFirst_ || frame >= keepLast_)
                return true;
            written_++;
            return file_.write(r, g, b, why);
        };
    }

    void said (void)
    {
        const std::string narrow = video_.narrow();

        if (!narrow.empty())
            fprintf(stderr, "%s\n", narrow.c_str());
    }

    thVideoSynth video_;
    thVideoBridge bridge_;
    thVideoFile file_;
    int64_t keepFirst_ = 0, keepLast_ = INT64_MAX, written_ = 0;
};

#endif /* VIDEO_RENDER_H */
