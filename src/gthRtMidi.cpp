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

#include "gthRtMidi.h"

gthRtMidi::gthRtMidi (const std::string &clientName, const std::string &api,
                      const std::string &port)
    : midi_(NULL)
{
    RtMidi::Api which = RtMidi::UNSPECIFIED;

    if (!api.empty())
    {
        which = RtMidi::getCompiledApiByName(api);

        if (which == RtMidi::UNSPECIFIED)
            fprintf(stderr, "midi: no '%s' API in this build, letting RtMidi "
                            "choose\n", api.c_str());
    }

    try
    {
        midi_ = new RtMidiIn(which, clientName);
    }
    catch (RtMidiError &e)
    {
        fprintf(stderr, "midi: %s\n", e.getMessage().c_str());
        midi_ = NULL;
        return;
    }

    /* SysEx would not fit in a gthMidiEvent and nothing acts on it; timing
       clock and active sensing arrive constantly and would do nothing but
       fill the ring. Dropping them at the source is cheaper than carrying
       them across a thread boundary to discard them. */
    midi_->ignoreTypes(true, true, true);

    try
    {
        if (!port.empty())
        {
            unsigned int n = midi_->getPortCount();
            bool found = false;

            for (unsigned int i = 0; i < n; i++)
            {
                const std::string name = midi_->getPortName(i);

                if (name == port || name.find(port) != std::string::npos)
                {
                    midi_->openPort(i, clientName);
                    portName_ = name;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                /* Fail closed. Falling through to a virtual port or to port 0
                   would connect to something other than what was asked for,
                   which is worse than not connecting: the user would see MIDI
                   working and not notice it was the wrong device. */
                fprintf(stderr, "midi: no input port matching '%s'; MIDI is "
                                "off. Try -L to see what is available.\n",
                        port.c_str());
                delete midi_;
                midi_ = NULL;
                return;
            }
        }

        if (portName_.empty())
        {
            /* A virtual port lets anything else on the system connect to us,
               which is how the ALSA sequencer version behaved. Windows MM has
               no such concept, so fall back to opening a real input. */
            try
            {
                midi_->openVirtualPort(clientName);
                portName_ = clientName + " (virtual)";
            }
            catch (RtMidiError &)
            {
                if (midi_->getPortCount() > 0)
                {
                    midi_->openPort(0, clientName);
                    portName_ = midi_->getPortName(0);
                }
                else
                {
                    fprintf(stderr, "midi: no input ports available\n");
                    delete midi_;
                    midi_ = NULL;
                    return;
                }
            }
        }
    }
    catch (RtMidiError &e)
    {
        fprintf(stderr, "midi: %s\n", e.getMessage().c_str());
        delete midi_;
        midi_ = NULL;
        return;
    }

    midi_->setCallback(&trampoline, this);

    printf("midi: %s via %s\n", portName_.c_str(), apiName().c_str());
}

gthRtMidi::~gthRtMidi (void)
{
    if (midi_ == NULL)
        return;

    /* Stop the callback before anything it touches goes away. */
    midi_->cancelCallback();
    midi_->closePort();

    delete midi_;
    midi_ = NULL;

    const unsigned long lost = overflows();

    if (lost)
        fprintf(stderr, "midi: %lu message(s) dropped to a full queue\n", lost);
}

std::string gthRtMidi::apiName (void) const
{
    if (midi_ == NULL)
        return "none";

    return RtMidi::getApiDisplayName(
        const_cast<RtMidiIn *>(midi_)->getCurrentApi());
}

std::vector<std::string> gthRtMidi::ports (void) const
{
    std::vector<std::string> out;

    if (midi_ == NULL)
        return out;

    RtMidiIn *m = const_cast<RtMidiIn *>(midi_);

    const unsigned int n = m->getPortCount();

    for (unsigned int i = 0; i < n; i++)
        out.push_back(m->getPortName(i));

    return out;
}

/* Enumeration only: no port is opened and no callback is installed, so this
   is safe to call for -L without side effects. */
std::vector<std::string> gthRtMidi::probePorts (const std::string &clientName,
                                                const std::string &api)
{
    std::vector<std::string> out;

    RtMidi::Api which = RtMidi::UNSPECIFIED;

    if (!api.empty())
        which = RtMidi::getCompiledApiByName(api);

    try
    {
        RtMidiIn probe(which, clientName);

        const unsigned int n = probe.getPortCount();

        for (unsigned int i = 0; i < n; i++)
            out.push_back(probe.getPortName(i));
    }
    catch (RtMidiError &e)
    {
        fprintf(stderr, "midi: %s\n", e.getMessage().c_str());
    }

    return out;
}

/* RtMidi's thread. No allocation, no lock, and nothing the GUI thread owns
   except the queue and its two atomics. */
void gthRtMidi::trampoline (double stamp, std::vector<unsigned char> *message,
                            void *user)
{
    (void)stamp;

    gthRtMidi *self = static_cast<gthRtMidi *>(user);

    if (message == NULL || message->empty() || message->size() > 3)
        return;

    gthMidiEvent ev;

    ev.status = (*message)[0];
    ev.data1 = message->size() > 1 ? (*message)[1] : 0;
    ev.data2 = message->size() > 2 ? (*message)[2] : 0;
    ev.len = (unsigned char)message->size();

    self->queue_.push(ev);
}
