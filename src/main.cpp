/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#ifdef USE_SIG_T
typedef sig_t sighandler_t;
#else
# ifdef GUESS_SIG_T
typedef void (*sighandler_t)(int);
# endif
#endif

#include <gtkmm.h>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif /* HAVE_ALSA */

#include "think.h"
#include "gthAudio.h"

#ifdef HAVE_ALSA
#include "gthALSAAudio.h"
#include "gthALSAMidi.h"
#endif /* HAVE_ALSA */

#ifdef HAVE_JACK
#include <jack/jack.h>
#include "gthJackAudio.h"
#endif /* HAVE_JACK */

#include "gthDummyAudio.h"

#include "gthSignal.h"
#include "gthPrefs.h"
#include "gthPatchfile.h"

#include "gui/Keyboard.h"
#include "gui/KeyboardWindow.h"
#include "gui/PatchSelWindow.h"
#include "gui/MainSynthWindow.h"

/* XXX: globals */
thSynth *Synth = NULL;
gthAudio *aout = NULL;

#ifdef HAVE_ALSA
static gthALSAMidi *midi = NULL;
#endif /* HAVE_ALSA */

Gtk::Main *gtkMain = NULL;

sigNoteOn  m_sigNoteOn;
sigNoteOff m_sigNoteOff;
sigNoteClear m_sigNoteClear;

static string plugin_path = PLUGIN_PATH;

static const char syntax[] = \
PACKAGE_NAME " " PACKAGE_VERSION " by Leif M. Ames, Misha Nasledov, "
"Aaron Lehmann and Joshua Kwan\n"
"Usage: %s [options]\n"
"-h\t\t\tdisplay this help screen\n"
"-p [path]\t\tmodify the plugin search path\n"
"-d [jack|alsa]\tchange output driver\n"
"  -o [file|device]\tchange output dest\n"
"-r [sample rate]\tset the sample rate\n"
"-l [window length]\tset the window length\n";
;

void cleanup (int signum)
{
    gthPrefs *prefs = gthPrefs::instance();

    signal(signum, SIG_IGN);
    switch (signum)
    {
        case SIGINT:
              printf("caught interrupt!\n");
        
        case SIGUSR1:
            printf("thinksynth shutting down..\n");
            
            if (signum == SIGUSR1)
            {
                printf("saving preferences\n");
                prefs->Save();
                delete prefs;
            }
            
            if (aout)
            {
                printf("closing audio devices...\n");
                delete aout;
            }

#ifdef HAVE_ALSA
            if (midi)
            {
                delete midi;
            }
#endif /* HAVE_ALSA */

            exit (0);
            break;
    }
}

void process_synth (void)
{
    Synth->process();
}

#ifdef HAVE_ALSA
/* ALSA callback */
void audio_readywrite (gthAudio *audio, thSynth *synth)
{
    int l = synth->getWindowlen();
    float *synthbuffer = synth->getOutput();

    audio->Write(synthbuffer, l);

    process_synth ();
}
#endif

#ifdef HAVE_JACK
/* JACK callback */
int playback_callback (jack_nframes_t nframes, void *arg)
{
    gthJackAudio *jack = static_cast<gthJackAudio *>(arg);

    int l = Synth->getWindowlen();
    int chans = Synth->audioChannelCount();

    for (int i = 0; i < chans; i++)
    {
        float *synthbuffer = Synth->getChanBuffer(i);
        void *buf = jack->GetOutBuf(i, nframes);

        memcpy(buf, synthbuffer, l * sizeof(float));
    }

    /* generate a new window; must make sure that everything herein is RT-safe.
       this means no mutex locks or memory allocations */
    process_synth ();

    return 0;
}
#endif /* HAVE_JACK */

#ifdef HAVE_ALSA
int processmidi (snd_seq_t *seq_handle, thSynth *synth)
{
    snd_seq_event_t *ev;

    while (snd_seq_event_input_pending(seq_handle, 1))
    {
        snd_seq_event_input(seq_handle, &ev);
        switch (ev->type)
        {
            case SND_SEQ_EVENT_NOTEON:
            {
                if ((unsigned int)ev->data.note.velocity)
                {
                    m_sigNoteOn(ev->data.note.channel, ev->data.note.note,
                                ev->data.note.velocity);
                    
                    synth->addNote(ev->data.note.channel, ev->data.note.note,
                                   ev->data.note.velocity);
                }
                /* a zero velocity can denote note off */
                else 
                {
                    m_sigNoteOff(ev->data.note.channel, ev->data.note.note);
                    
                    synth->delNote(ev->data.note.channel, ev->data.note.note);
                }
                break;
            }
            case SND_SEQ_EVENT_NOTEOFF:
            {
                m_sigNoteOff(ev->data.note.channel, ev->data.note.note);
                
                synth->delNote(ev->data.note.channel, ev->data.note.note);
                break;
            }
            case SND_SEQ_EVENT_TEMPO:
            {
                debug("TEMPO CHANGE:  %i\n", ev->data.control.value);
                break;
            }
            case SND_SEQ_EVENT_TICK:
            {
                debug("TICK CHANGE:  %i\n", ev->data.control.value);
                break;
            }
            case SND_SEQ_EVENT_PITCHBEND:
            {
                debug("PITCH BEND:  %i\n", ev->data.control.value);
                break;
            }
            case SND_SEQ_EVENT_PGMCHANGE:
            {
                debug("PGM CHANGE  %d\n", ev->data.control.value);
                break;
            }
            case SND_SEQ_EVENT_CONTROLLER:
            {
                synth->handleMidiController(ev->data.control.channel,
                                            ev->data.control.param,
                                            ev->data.control.value);
                
                break;
            }
            case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
            {
                synth->clearAll();
                m_sigNoteClear();
                break;
            }
            default:
            {
                debug("got unknown event %d\n", ev->type);
                break;
            }
        }
        
        snd_seq_free_event(ev);
    }

    return 0;
}
#endif /* HAVE_ALSA */

int main (int argc, char *argv[])
{
    string outputfname;
    string driver = DEFAULT_OUTPUT;
    int havearg = -1;
    int samples = TH_DEFAULT_SAMPLES, windowlen = TH_DEFAULT_WINDOW_LENGTH;

    /* seed the random number generator */
    srand(time(NULL));

    Glib::thread_init();
    /* init Glib/Gtk args */
    Gtk::Main mymain(argc, argv);

    while ((havearg = getopt (argc, argv, "hp:o:d:r:l:")) != -1)
    {
        switch (havearg)
        {
            case 'r':
            {
                samples = atoi(optarg);
                break;
            }
            case 'l':
            {
                windowlen = atoi(optarg);
                break;
            }
            case 'd':
            {
                driver = optarg;
                break;
            }
            case 'o':
            {
                outputfname = optarg;
                break;
            }
            case 'h':
            {
                printf(syntax, argv[0]);
                return 0;
                break;
            }
            case 'p':
            {
                if (optarg[strlen(optarg)-1] != '/') {
                    plugin_path = optarg;
                    plugin_path += '/';
                }
                else {
                    plugin_path = optarg;
                }
                
                break;
            }
            default:
            {
                if (optind != argc)
                {
                    printf(syntax, argv[0]);
                    return 1;
                }
                break;
            }
        }
    }

    /* XXX: create global Synth object */
    Synth = new thSynth(plugin_path, windowlen, samples);
    gthPrefs *prefs = gthPrefs::instance();

    signal(SIGUSR1, (sighandler_t)cleanup);
    signal(SIGINT, (sighandler_t)cleanup);

    /* create a window first */
    Synth->process();

    /* all thAudio classes will work with floating point buffers, converting
       to other formats internally, if necessary */
    try
    {
#ifdef HAVE_ALSA
        midi = new gthALSAMidi("thinksynth");

        if (midi->seq_opened())
        {
            midi->signal_midi_event().connect(
                sigc::bind<thSynth *>(sigc::ptr_fun(&processmidi), Synth));
        }
#endif /* HAVE_ALSA */

        printf ("Trying the '%s' driver\n", driver.c_str());

        if (driver == "alsa")
        { 
#ifdef HAVE_ALSA
            if (outputfname.length() > 0)
                aout = new gthALSAAudio(Synth, outputfname.c_str());
            else
                aout = new gthALSAAudio(Synth);

            /* connect our audio out event handler and bind a synth to this
               instance */
            gthALSAAudio *aptr = static_cast<gthALSAAudio *>(aout);
            aptr->signal_ready_write().connect(
                sigc::bind<gthAudio *,thSynth *>(sigc::ptr_fun(&audio_readywrite),
                                                 aout, Synth)); 
#else
            fprintf(stderr, "Sorry, ALSA is not supported in this build.\n");
#endif /* HAVE_ALSA */
        }
         else if (driver == "jack")
        {
#ifdef HAVE_JACK
            aout = new gthJackAudio(Synth, playback_callback);
#else
            fprintf(stderr, "Sorry, JACK is not supported in this build.\n");
            return 1;
#endif /* HAVE_JACK */
        }
        else
        {
            if (driver != "none")
                fprintf(stderr, "Unrecognized driver '%s'!\n", driver.c_str());

            puts("Using dummy audio device; no audio output will occur.");
            aout = new gthDummyAudio(Synth);
        }

    }
    catch (thIOException e)
    {
        fprintf(stderr, "Error creating audio device: %s\n", strerror(e));

        if (driver == "jack")
            fprintf(stderr, "Perhaps you should start jackd? Try jackd -d alsa.\n");

        fprintf(stderr, "Falling back to dummy audio device\n");

        aout = new gthDummyAudio(Synth);
    }

    MainSynthWindow synthWindow(aout);

    prefs->Load();

    mymain.run(synthWindow);

#ifdef HAVE_ALSA
    delete midi;
#endif /* HAVE_ALSA */
    
    printf("saving preferences\n");
    prefs->Save();

    delete prefs;

    if (aout)
    {
        printf("closing audio devices...\n");
        delete aout;
    }

    /* Will not be reached if using 'alsa' driver */
#if 0
    printf("deleting synth\n");
    delete Synth;
#endif

    return 0;
}

