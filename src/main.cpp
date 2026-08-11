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
#include <locale.h>
#include <signal.h>

#ifdef USE_SIG_T
typedef sig_t sighandler_t;
#else
# ifdef GUESS_SIG_T
typedef void (*sighandler_t)(int);
# endif
#endif

#include <gtkmm.h>

#include "think.h"
#include "gthAudio.h"
#include "gthGtkRuntime.h"
#include "gthSynthSource.h"
#include "gthRtAudio.h"
#include "gthRtMidi.h"

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
static gthSynthSource *asource = NULL;

static gthRtMidi *midi = NULL;

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
"-d [driver]\t\taudio API: alsa, jack, pulse, core, wasapi, asio,\n"
"\t\t\tnone, or empty to let RtAudio choose\n"
"  -o [device]\t\toutput device, by full or partial name\n"
"-m [port]\t\tMIDI input port, by full or partial name; if it does\n"
"\t\t\tnot match, MIDI is off rather than something else\n"
"-L\t\t\tlist the audio and MIDI APIs, devices and ports, then exit\n"
"-G\t\t\treport whether GTK's schemas, icon theme and image loaders\n"
"\t\t\tare reachable, then exit nonzero if any are not\n"
"-r [sample rate]\tset the sample rate\n"
"-l [window length]\tset the window length\n";
;

/* Set by the signal handler, read by the GUI thread.
 *
 * The handler used to do the whole shutdown itself: printf, prefs->Save(),
 * delete the audio device, exit(). Almost none of that is
 * async-signal-safe -- printf takes a lock, Save() allocates, and with
 * RtAudio the destructor joins the callback thread. Reached from a signal
 * that interrupted any of those, it can deadlock or corrupt the heap, and
 * "^C sometimes hangs" is a miserable thing to debug.
 *
 * So the handler now does the only two things it is allowed to do: write a
 * flag and return. The real teardown happens on the GUI thread, from a
 * timeout below, which is where every one of those calls is safe. */
static volatile sig_atomic_t shutdownRequested = 0;

extern "C" void cleanup (int signum)
{
    /* Both of these are async-signal-safe, and nothing else here is. */
    signal(signum, SIG_DFL);
    shutdownRequested = signum;
}

/* GUI thread, polled from a Glib timeout. */
static bool checkShutdown (void)
{
    if (!shutdownRequested)
        return true;   /* keep polling */

    const int signum = (int)shutdownRequested;

    if (signum == SIGINT)
        printf("caught interrupt!\n");

    printf("thinksynth shutting down..\n");

    /* Ask the main loop to unwind. Everything below main()'s run() then
       happens in the ordinary way, on this thread, with the audio device
       stopped before anything it touches is freed. */
    if (gtkMain)
        gtkMain->quit();

    return false;
}

void process_synth (void)
{
    Synth->process();
}

/* audio_readywrite() and playback_callback() used to be here.
 *
 * The first pushed a window at gthALSAAudio::Write() whenever a poll on
 * ALSA's descriptors said there was room. The second was JACK's callback,
 * reaching for the global Synth and copying min(nframes, windowlen) frames
 * per channel -- which silently dropped or padded audio whenever those two
 * disagreed, and only ever agreed because JACK's default period is 1024 and
 * so is TH_DEFAULT_WINDOW_LENGTH.
 *
 * Both are now gthSynthSource, which every backend pulls from and which
 * copes with any block size. scripts/dspblock is the regression test.
 */


/* -L */
static void listAudio (void)
{
    const std::vector<std::string> apis = gthRtAudio::availableApis();

    printf("audio APIs in this build:");

    for (size_t i = 0; i < apis.size(); i++)
        printf(" %s", apis[i].c_str());

    printf("\n\noutput devices on the default API:\n");

    {
        gthRtAudio probe;

        /* Deliberately scoped, and deliberately not an early return: a
           machine with no working audio device is exactly the machine whose
           owner wants to see the MIDI list. */
        if (!probe.valid())
            printf("  (none -- RtAudio would not start)\n");
        else
        {
            const std::vector<gthAudioDevice> devs = probe.devices();

            for (size_t i = 0; i < devs.size(); i++)
                printf("  %-52s %u ch%s\n", devs[i].name.c_str(),
                       devs[i].outputChannels,
                       devs[i].isDefault ? "  (default)" : "");

            if (devs.empty())
                printf("  (none)\n");
        }
    }

    printf("\nMIDI input ports:\n");

    /* Enumerated without opening anything -- see gthRtMidi::probePorts. */
    const std::vector<std::string> mports = gthRtMidi::probePorts(PACKAGE_NAME);

    for (size_t i = 0; i < mports.size(); i++)
        printf("  %s\n", mports[i].c_str());

    if (mports.empty())
        printf("  (none)\n");
}

/* One MIDI message, on the GUI thread.
 *
 * This was processmidi(), which walked ALSA sequencer events off a poll
 * descriptor. The event types are now the wire bytes, which is what every
 * MIDI API on every platform agrees on -- so the switch is on the status
 * nibble rather than on SND_SEQ_EVENT_*, and everything it calls is
 * unchanged.
 */
static void dispatchmidi (const gthMidiEvent &ev, thSynth *synth)
{
    const unsigned char kind = ev.status & 0xf0;
    const int chan = ev.status & 0x0f;

    switch (kind)
    {
        case 0x90:   /* note on */
        {
            if (ev.data2)
            {
                m_sigNoteOn(chan, ev.data1, ev.data2);
                synth->addNote(chan, ev.data1, ev.data2);
            }
            else
            {
                /* a zero velocity can denote note off */
                m_sigNoteOff(chan, ev.data1);
                synth->delNote(chan, ev.data1);
            }
            break;
        }
        case 0x80:   /* note off */
        {
            m_sigNoteOff(chan, ev.data1);
            synth->delNote(chan, ev.data1);
            break;
        }
        case 0xb0:   /* controller */
        {
            synth->handleMidiController(chan, ev.data1, ev.data2);
            break;
        }
        case 0xe0:   /* pitch bend */
        {
            debug("PITCH BEND: %d\n", (ev.data2 << 7) | ev.data1);
            break;
        }
        case 0xc0:   /* program change */
        {
            debug("PGM CHANGE %d\n", ev.data1);
            break;
        }
        default:
        {
            debug("got unknown MIDI status 0x%02x\n", ev.status);
            break;
        }
    }
}

int main (int argc, char *argv[])
{
    string outputfname;
    string midiport;
    string midiapi;
    string driver = DEFAULT_OUTPUT;
    int havearg = -1;
    int samples = TH_DEFAULT_SAMPLES, windowlen = TH_DEFAULT_WINDOW_LENGTH;

    /* seed the random number generator */
    srand(time(NULL));

    /* Glib::thread_init() used to be here. glibmm's own header says it "is no
       longer necessary and no longer has any effect" -- GLib has initialised
       its threading itself since 2.32, in 2011 -- and MSYS2 builds glibmm
       with the deprecated API compiled out, so on Windows it is not merely
       pointless but an undefined reference at link time. */

    /* Before Gtk::Main, and it has to be: GLib caches the system data
       directories the first time anything asks for them, and Gtk::Main asks.
       Does nothing unless the package shipped GTK's data alongside us, which
       on Linux it does not. */
    gthGtkRuntime::configure();

    /* init Glib/Gtk args */
    Gtk::Main mymain(argc, argv);

    /* Force the numeric locale back to C, and do it *after* Gtk::Main, which
     * calls setlocale(LC_ALL, "") on our behalf.
     *
     * Every number thinksynth reads or writes goes through the C library's
     * locale-sensitive conversions: atof() in the .dsp lexer, strtof() in the
     * patch parser, atof() and "%f" in the preferences. Under a locale with a
     * comma decimal separator -- de_DE, fr_FR, most of Europe -- `@pw1 = 0.3'
     * in a .dsp parses as 0.0, because atof stops at the '.'. Every fractional
     * parameter in every DSP and patch silently truncates, which does not fail
     * loudly, it just makes the synth sound wrong.
     *
     * The preferences are worse still: "%f" would *write* "1,250000", and the
     * prefs parser splits values on commas, so a saved master gain came back
     * as two fields and lost its fraction on reload.
     *
     * Only LC_NUMERIC is pinned; LC_MESSAGES, LC_TIME and the rest stay as the
     * user set them, so the interface is unaffected. This is the usual thing
     * for applications with a numeric file format.
     *
     * NB this fixes the *application*. libthink itself still uses atof() in
     * its lexer, so anything embedding the library has to do the same. */
    setlocale(LC_NUMERIC, "C");

    while ((havearg = getopt (argc, argv, "hLGp:o:d:m:r:l:")) != -1)
    {
        switch (havearg)
        {
            case 'L':
            {
                listAudio();
                return 0;
            }
            case 'G':
            {
                return gthGtkRuntime::selfTest();
            }
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
            case 'm':
            {
                midiport = optarg;
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

    signal(SIGINT, (sighandler_t)cleanup);

    /* SIGUSR1 is "save preferences and exit", triggered from outside. Windows
       has no such signal -- its signal() knows six, and the user-defined ones
       are not among them -- so there is nothing to hook there and no obvious
       equivalent worth inventing. SIGINT covers the ordinary case on all
       three platforms. */
#ifdef SIGUSR1
    signal(SIGUSR1, (sighandler_t)cleanup);
#endif

    /* The handler only sets a flag; this is what notices. 200ms is
       imperceptible for a ^C and costs nothing when idle. */
    Glib::signal_timeout().connect(sigc::ptr_fun(&checkShutdown), 200);

    /* The source primes the synth in prepare(); no need to render a window
       here as well. */
    asource = new gthSynthSource(Synth);

    gthAudioFmt want;

    want.rate = samples;
    want.channels = Synth->audioChannelCount();
    want.frames = 0;              /* let the device choose; see gthSynthSource */

    try
    {
        midi = new gthRtMidi(PACKAGE_NAME, midiapi, midiport);

        if (midi->opened())
        {
            midi->signal_event().connect(
                sigc::bind<thSynth *>(sigc::ptr_fun(&dispatchmidi), Synth));
        }

        if (driver == "none")
        {
            puts("Using dummy audio device; no audio output will occur.");
            aout = new gthDummyAudio();
        }
        else
        {
            /* "alsa", "jack", "pulse", "core", "wasapi", "asio" all name an
               RtAudio API now; an empty or unrecognised value lets RtAudio
               pick. So -d jack and -d alsa still mean what they meant, but
               the implementation behind them is no longer ours. */
            printf("Trying the '%s' audio API\n",
                   driver.empty() ? "(default)" : driver.c_str());

            gthRtAudio *rt = new gthRtAudio(driver, outputfname);

            if (rt->valid() && rt->open(want, asource) && rt->start())
            {
                aout = rt;
            }
            else
            {
                delete rt;

                fprintf(stderr, "Could not open an audio device.\n");

                if (driver == "jack")
                    fprintf(stderr, "Perhaps you should start jackd? "
                                    "Try jackd -d alsa.\n");

                fprintf(stderr, "Falling back to dummy audio device; "
                                "try -L to see what is available.\n");

                aout = new gthDummyAudio();
            }
        }

        /* The dummy and legacy-JACK paths still need opening and starting;
           the RtAudio one has already done both. */
        if (aout != NULL && !aout->running())
        {
            aout->open(want, asource);
            aout->start();
        }
    }
    catch (thIOException e)
    {
        fprintf(stderr, "Error creating audio device: %s\n", strerror(e));
        fprintf(stderr, "Falling back to dummy audio device\n");

        aout = new gthDummyAudio();
        aout->open(want, asource);
        aout->start();
    }

    /* On the heap, so it can be destroyed at a chosen point rather than at
       the end of main.
     *
     * It was a local, which put its destructor after `delete Synth' -- and
     * the window is made of things that point into the synth: sliders bound
     * to a channel's args, a node editor holding the thSynth it parses with,
     * a patch bar subscribed to a channel's amplitude. Tearing all that down
     * after the synth had gone crashed on exit, by way of a node editor built
     * against a NULL thSynth::instance() as the notebook came apart. The
     * window has to go first. */
    MainSynthWindow *synthWindow = new MainSynthWindow(aout);

    prefs->Load();

    /* checkShutdown() needs this to unwind the loop from the timeout. */
    gtkMain = &mymain;

    mymain.run(*synthWindow);

    gtkMain = NULL;

    /* Silence the device first. Everything below this touches state the
       audio callback reads -- saving preferences walks the patch manager,
       which walks the synth -- and stop() does not return until the callback
       has. */
    if (aout)
    {
        printf("closing audio devices...\n");
        aout->stop();
    }

    delete midi;
    midi = NULL;

    /* Long before the synth, which is the whole point. */
    delete synthWindow;
    synthWindow = NULL;

    printf("saving preferences\n");
    prefs->Save();

    delete prefs;

    delete aout;
    aout = NULL;

    delete asource;
    asource = NULL;

    printf("deleting synth\n");
    delete Synth;
    Synth = NULL;

    return 0;
}

