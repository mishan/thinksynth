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

#include "config.h"

#include <stdio.h>

#include "think.h"

#include "gthAudio.h"
#include "gthAudioRate.h"
#include "gthSynthSource.h"

bool gthFollowDeviceRate (gthAudio *audio, thSynth *&synth,
                          gthSynthSource *&source,
                          const std::string &pluginPath)
{
    if (audio == NULL || synth == NULL)
        return false;

    gthAudioFmt fmt = audio->format();

    if (fmt.rate <= 0 || fmt.rate == synth->getSampleRate())
        return false;

    printf("audio: making the synth again at %d Hz, the device's rate\n",
           fmt.rate);

    /* Stopped first: the callback renders through `source' into `synth',
       and stop() does not return while it is inside them. */
    audio->stop();

    const int windowlen = synth->getWindowlen();
    const int threads = synth->threads();

    delete source;
    delete synth;

    synth = new thSynth(pluginPath, windowlen, fmt.rate);
    synth->setThreads(threads);
    source = new gthSynthSource(synth);

    /* The period the device settled on, asked for again, so the reopen
       lands where the first open did. */
    fmt.channels = synth->audioChannelCount();

    if (!audio->open(fmt, source) || !audio->start())
    {
        fprintf(stderr, "audio: could not reopen the device at %d Hz\n",
                fmt.rate);
        return true;
    }

    if (audio->format().rate != fmt.rate)
        printf("audio: device reopened at %d Hz, not %d\n",
               audio->format().rate, fmt.rate);

    return true;
}
