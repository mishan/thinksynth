/* $Id: main.cpp,v 1.182 2004/05/26 00:14:04 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include <alsa/asoundlib.h>
#include <jack/jack.h>

#include <gtkmm.h>

#include "think.h"

#include "thfAudio.h"
#include "thfALSAAudio.h"
#include "thfALSAMidi.h"
#include "thfJackAudio.h"

#include "ui.h"
#include "signal.h"
#include "prefs.h"

/* XXX: globals */
thSynth *Synth = NULL;

static Glib::Thread *ui = NULL;
static Glib::Dispatcher *process = NULL;

Glib::RefPtr<Glib::MainContext> mainContext;

Glib::Mutex *mainMutex = NULL;
Glib::Cond *exitCond  = NULL;

Gtk::Main *gtkMain = NULL;

sigNoteOn  m_sigNoteOn;
sigNoteOff m_sigNoteOff;

/* XXX: this is a *GROSS* error; the library level should not depend on an
   application-level global; the thSynth class should have some sort of
   plugin path and it should use the default of PLUGIN_PATH if nothing is
   passed to it */
static string plugin_path = PLUGIN_PATH;

static const char syntax[] = \
PACKAGE_NAME " " PACKAGE_VERSION " by Leif M. Ames, Misha Nasledov, "
"Aaron Lehmann and Joshua Kwan\n"
"Usage: %s [options] dsp-file\n"
"-h\t\t\tdisplay this help screen\n"
"-p [path]\t\tmodify the plugin search path\n"
"-d [alsa|oss|wav]\tchange output driver\n"
"  -o [file|device]\tchange output dest\n"
"-r [sample rate]\tset the sample rate\n"
"-l [window length]\tset the window length\n";
;

void cleanup (int signum)
{
	printf("received SIGTERM! exiting...\n\n");

	save_prefs(Synth);

	/* XXX */
	exitCond->signal();

	exit (0);
}

void process_synth (void)
{
//	mainMutex->lock();
	Synth->Process();
//	mainMutex->unlock();
}

void audio_readywrite (thfAudio *audio, thSynth *synth)
{
	int l = synth->GetWindowLen();
	float *synthbuffer = synth->GetOutput();

//	mainMutex->lock();
	audio->Write(synthbuffer, l);
//	mainMutex->unlock();

	/* XXX: we should be using emit() but this fucks up */
//	process->emit();
	process_synth ();
}

int playback_callback (jack_nframes_t nframes, void *arg)
{
	thfJackAudio *jack = (thfJackAudio *)arg;
	int l = Synth->GetWindowLen();
	int chans = Synth->GetChans();

	for(int i = 0; i < chans; i++)
	{
		float *synthbuffer = Synth->GetChanBuffer(i);
		void *buf = jack->GetOutBuf(i, nframes);

		memcpy(buf, synthbuffer, l * sizeof(float));
	}
	
	/* XXX: we should be using emit() but this fucks up */
	/* call the main thread to generate a new window */
//	process->emit();
	process_synth ();

	return 0;
}

int processmidi (snd_seq_t *seq_handle, thSynth *synth)
{
	snd_seq_event_t *ev;

	while (snd_seq_event_input(seq_handle, &ev))
	{
		switch (ev->type)
		{
			case SND_SEQ_EVENT_NOTEON:
			{
				if ((unsigned int)ev->data.note.velocity)
				{
					m_sigNoteOn(ev->data.note.channel, ev->data.note.note,
								ev->data.note.velocity);
					
					synth->AddNote(ev->data.note.channel, ev->data.note.note,
								   ev->data.note.velocity);
				}
				/* a zero velocity can denote note off */
				else 
				{
					m_sigNoteOff(ev->data.note.channel, ev->data.note.note);
					
					synth->DelNote(ev->data.note.channel, ev->data.note.note);
				}
				break;
			}
			case SND_SEQ_EVENT_NOTEOFF:
			{
				m_sigNoteOff(ev->data.note.channel, ev->data.note.note);
				
				synth->DelNote(ev->data.note.channel, ev->data.note.note);
				break;
			}
			case SND_SEQ_EVENT_TEMPO:
			{
				printf("TEMPO CHANGE:  %i\n", ev->data.control.value);
				break;
			}
			case SND_SEQ_EVENT_TICK:
			{
				printf("TICK CHANGE:  %i\n", ev->data.control.value);
				break;
			}
			case SND_SEQ_EVENT_PITCHBEND:
			{
				printf("PITCH BEND:  %i\n", ev->data.control.value);
				break;
			}
			case SND_SEQ_EVENT_PGMCHANGE:
			{
				printf("PGM CHANGE  %d\n", ev->data.control.value);
				break;
			}
			case SND_SEQ_EVENT_CONTROLLER:
			{
//				printf("CONTROLLER  %d\n", ev->data.control.value);
				break;
			}
			default:
			{
				debug("got unknown event 0x%02x", ev->type);
				break;
			}
		}
		
		snd_seq_free_event(ev);
	}
	
	return 0;
}

int main (int argc, char *argv[])
{
	string outputfname;
	string driver = "jack";
	int havearg = -1;
	thfALSAMidi *midi = NULL;
	thfAudio *aout = NULL;
	int samples = TH_DEFAULT_SAMPLES, windowlen = TH_DEFAULT_WINDOW_LENGTH;

	/* seed the random number generator */
	srand(time(NULL));

	/* init Glib/Gtk args */
	gtkMain = new Gtk::Main (argc, argv);

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
				exit(0);
				break;
			}
			case 'p':
			{
				if (optarg[strlen(optarg)-1] != '/') {
					plugin_path = optarg + '/';
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
					exit(1);
				}
				break;
			}
		}
	}

	/* XXX: create global Synth object */
	Synth = new thSynth(plugin_path, windowlen, samples);

	/* Glib/Gtk signal/thread handling init */
	Glib::thread_init();
	mainContext = Glib::MainContext::create ();

	mainMutex = new Glib::Mutex;
	exitCond = new Glib::Cond;

 	process = new Glib::Dispatcher(mainContext);
	process->connect(SigC::slot(process_synth));

	signal(SIGTERM, (sighandler_t)cleanup);

	read_prefs (Synth);

	/* create UI thread */
	ui = Glib::Thread::create(SigC::slot(&ui_thread), false);

	/* create a window first */
	Synth->Process();

	/* all thAudio classes will work with floating point buffers converting to
	   integer internally based on format data */
	try
	{
		midi = new thfALSAMidi("thinksynth");

		midi->signal_midi_event().connect(
			SigC::bind<thSynth *>(SigC::slot(&processmidi), Synth));

		printf ("Using the '%s' driver\n", driver.c_str());

		if (driver == "alsa")
		{ 
			if (outputfname.length() > 0)
				aout = new thfALSAAudio(Synth, outputfname.c_str());
			else
				aout = new thfALSAAudio(Synth);

			/* connect our audio out event handler and bind a synth to this
			   instance */
			((thfALSAAudio *)aout)->signal_ready_write().connect(
				SigC::bind<thfAudio *,thSynth *>(SigC::slot(&audio_readywrite),
												 aout, Synth)); 
		}
 		else if (driver == "jack")
		{
			aout = new thfJackAudio(Synth, playback_callback);
		}
		else
		{
			fprintf(stderr,"sorry only JACK/ALSA drivers are supported currently for output.\n");
		}

	}
	catch (thIOException e)
	{
		fprintf(stderr, "error creating audio device: %s\n", strerror(e));
		exit (1);
	}

	if (outputfname.length() > 0)
	{
		printf ("Writing to '%s'\n", outputfname.c_str());
	}

#if 0
	while (1)
	{
		sleep (100);
	}
#endif 

	while (1)
	{
//		debug("iterating");
		mainContext->iteration (true); /* blocking */
	}

	printf("break!\n");

//	exitCond->wait(*mainMutex);

	delete aout;
	delete midi;
//	delete gtkMain;

	save_prefs (Synth);

	return 0;
}

