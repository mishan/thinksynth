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

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "gthRtAudio.h"

static void errorCallback (RtAudioErrorType type, const std::string &text)
{
    /* RtAudio 6 reports rather than throws. Warnings are routine -- a device
       that cannot do the requested rate, a JACK server that went away -- so
       they are not fatal here; open() and start() check their return codes. */
    if (type == RTAUDIO_WARNING)
        fprintf(stderr, "audio: %s\n", text.c_str());
    else
        fprintf(stderr, "audio error: %s\n", text.c_str());
}

gthRtAudio::gthRtAudio (const std::string &api, const std::string &device)
    : rt_(NULL), source_(NULL), wantDevice_(device), deviceId_(0),
      underruns_(0)
{
    fmt_.rate = 0;
    fmt_.channels = 0;
    fmt_.frames = 0;

    RtAudio::Api which = RtAudio::UNSPECIFIED;

    if (!api.empty() && api != "rtaudio")
    {
        which = RtAudio::getCompiledApiByName(api);

        if (which == RtAudio::UNSPECIFIED)
        {
            /* "core" for coreaudio, "pulse" for pulse, and so on -- accept
               anything that prefixes a compiled-in API's name, so the
               command line does not have to know RtAudio's exact spelling. */
            std::vector<RtAudio::Api> apis;

            RtAudio::getCompiledApi(apis);

            for (size_t i = 0; i < apis.size(); i++)
            {
                const std::string name = RtAudio::getApiName(apis[i]);

                if (name.compare(0, api.size(), api) == 0)
                {
                    which = apis[i];
                    break;
                }
            }
        }

        if (which == RtAudio::UNSPECIFIED)
            fprintf(stderr, "audio: no '%s' API in this build, letting "
                            "RtAudio choose\n", api.c_str());
    }

    rt_ = new RtAudio(which, &errorCallback);

    if (rt_->getDeviceIds().empty())
    {
        fprintf(stderr, "audio: the %s API reports no devices\n",
                apiName().c_str());
        delete rt_;
        rt_ = NULL;
    }
}

gthRtAudio::~gthRtAudio (void)
{
    if (rt_ == NULL)
        return;

    stop();

    if (rt_->isStreamOpen())
        rt_->closeStream();

    delete rt_;
}

std::string gthRtAudio::apiName (void) const
{
    if (rt_ == NULL)
        return "none";

    return RtAudio::getApiDisplayName(rt_->getCurrentApi());
}

std::vector<std::string> gthRtAudio::availableApis (void)
{
    std::vector<RtAudio::Api> apis;
    std::vector<std::string> names;

    RtAudio::getCompiledApi(apis);

    for (size_t i = 0; i < apis.size(); i++)
        names.push_back(RtAudio::getApiName(apis[i]));

    return names;
}

std::vector<gthAudioDevice> gthRtAudio::devices (void) const
{
    std::vector<gthAudioDevice> out;

    if (rt_ == NULL)
        return out;

    const std::vector<unsigned int> ids =
        const_cast<RtAudio *>(rt_)->getDeviceIds();

    for (size_t i = 0; i < ids.size(); i++)
    {
        const RtAudio::DeviceInfo info =
            const_cast<RtAudio *>(rt_)->getDeviceInfo(ids[i]);

        if (info.outputChannels == 0)
            continue;

        gthAudioDevice d;

        d.id = info.ID;
        d.name = info.name;
        d.outputChannels = info.outputChannels;
        d.isDefault = info.isDefaultOutput;

        out.push_back(d);
    }

    return out;
}

/* Match by exact name first, then by substring, so `-o HDA' finds
   "HDA Intel PCH: ALC257 Analog (hw:0,0)" without anyone having to type it. */
unsigned gthRtAudio::resolveDevice (const std::string &name) const
{
    if (rt_ == NULL)
        return 0;

    const std::vector<gthAudioDevice> devs = devices();

    if (name.empty())
    {
        const unsigned def = const_cast<RtAudio *>(rt_)->getDefaultOutputDevice();

        if (def != 0)
            return def;

        return devs.empty() ? 0 : devs[0].id;
    }

    for (size_t i = 0; i < devs.size(); i++)
        if (devs[i].name == name)
            return devs[i].id;

    for (size_t i = 0; i < devs.size(); i++)
        if (devs[i].name.find(name) != std::string::npos)
            return devs[i].id;

    fprintf(stderr, "audio: no output device matching '%s'; using the "
                    "default\n", name.c_str());

    const unsigned def = const_cast<RtAudio *>(rt_)->getDefaultOutputDevice();

    return def != 0 ? def : (devs.empty() ? 0 : devs[0].id);
}

int gthRtAudio::trampoline (void *out, void *in, unsigned frames,
                            double streamTime, RtAudioStreamStatus status,
                            void *user)
{
    (void)in;
    (void)streamTime;

    gthRtAudio *self = static_cast<gthRtAudio *>(user);

    /* Worth knowing about but not worth stopping for, and printing from the
       audio thread is itself a bad idea -- so it is counted here and reported
       once in stop(). An underrun means the callback did not return in time,
       which with a whole synth window of work per boundary is the expected
       way for this to fail on a slow machine. */
    if (status & RTAUDIO_OUTPUT_UNDERFLOW)
        self->underruns_.fetch_add(1, std::memory_order_relaxed);

    self->source_->render(static_cast<float *>(out), frames,
                          (unsigned)self->fmt_.channels);

    return 0;
}

bool gthRtAudio::open (const gthAudioFmt &want, gthAudioSource *source)
{
    if (rt_ == NULL || source == NULL)
        return false;

    source_ = source;
    deviceId_ = resolveDevice(wantDevice_);

    if (deviceId_ == 0)
    {
        fprintf(stderr, "audio: no usable output device\n");
        return false;
    }

    const RtAudio::DeviceInfo info = rt_->getDeviceInfo(deviceId_);

    deviceName_ = info.name;

    RtAudio::StreamParameters params;

    params.deviceId = deviceId_;
    params.nChannels = (unsigned)want.channels;
    params.firstChannel = 0;

    if (info.outputChannels < params.nChannels)
    {
        fprintf(stderr, "audio: %s has %u output channels, not %u\n",
                info.name.c_str(), info.outputChannels, params.nChannels);
        params.nChannels = info.outputChannels;
    }

    RtAudio::StreamOptions opts;

    opts.flags = RTAUDIO_SCHEDULE_REALTIME;
    opts.numberOfBuffers = 2;
    opts.streamName = PACKAGE_NAME;
    opts.priority = 80;

    /* A hint. RtAudio writes back what the device actually gave, and that is
       the number the source has to cope with -- it is very often not the
       synth's window length, which is the bug gthSynthSource exists to fix. */
    unsigned frames = want.frames ? want.frames : 512;

    const RtAudioErrorType err =
        rt_->openStream(&params, NULL, RTAUDIO_FLOAT32,
                        (unsigned)want.rate, &frames, &trampoline, this,
                        &opts);

    if (err != RTAUDIO_NO_ERROR)
    {
        fprintf(stderr, "audio: could not open %s\n", info.name.c_str());
        source_ = NULL;
        return false;
    }

    fmt_.rate = (int)rt_->getStreamSampleRate();
    fmt_.channels = (int)params.nChannels;
    fmt_.frames = frames;

    if (fmt_.rate != want.rate)
        printf("audio: device runs at %d Hz, not the requested %d\n",
               fmt_.rate, want.rate);

    source_->prepare(fmt_.frames, (unsigned)fmt_.channels);

    printf("audio: %s via %s, %d Hz, %d ch, %u frames per callback\n",
           deviceName_.c_str(), apiName().c_str(), fmt_.rate, fmt_.channels,
           fmt_.frames);

    return true;
}

bool gthRtAudio::start (void)
{
    if (rt_ == NULL || !rt_->isStreamOpen())
        return false;

    if (rt_->isStreamRunning())
        return true;

    return rt_->startStream() == RTAUDIO_NO_ERROR;
}

void gthRtAudio::stop (void)
{
    if (rt_ == NULL || !rt_->isStreamOpen())
        return;

    if (rt_->isStreamRunning())
        rt_->stopStream();

    const unsigned long lost = underruns();

    if (lost)
        fprintf(stderr, "audio: %lu underrun(s) -- the callback did not "
                        "return in time\n", lost);
}

bool gthRtAudio::running (void) const
{
    return rt_ != NULL && const_cast<RtAudio *>(rt_)->isStreamRunning();
}
