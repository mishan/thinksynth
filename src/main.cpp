/* $Id: main.cpp,v 1.154 2004/04/07 04:09:29 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include <alsa/asoundlib.h>

#include <gtkmm.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

#include "thException.h"
#include "thAudio.h"
#include "thWav.h"
#include "thALSAAudio.h"
#include "thOSSAudio.h"
#include "thEndian.h"

#include "parser.h"

#include "ui.h"
#include "signal.h"

/* XXX: globals */
Gtk::Main *gtkMain = NULL;

sigNoteOn  m_sigNoteOn;
sigNoteOff m_sigNoteOff;

sigReadyWrite m_sigReadyWrite;
sigMidiEvent m_sigMidiEvent;

void cleanup (int signum)
{
	printf("received SIGTERM!\n\n exiting...\n");

	if (gtkMain)
		delete gtkMain;

	exit (0);
}

void audio_readywrite (thAudio *audio, thSynth *synth)
{
	int l = synth->GetWindowLen();
	float *synthbuffer = synth->GetOutput();

	audio->Write(synthbuffer, l);
}

/* XXX: rewrite main event routine; pass Audio object a set of signal objects
   with appropriate callbacks set; e.g: readyRead callback, readyWrite, etc. */

string plugin_path;

const char syntax[] = \
PACKAGE_NAME " " PACKAGE_VERSION " by Leif M. Ames, Misha Nasledov, "
"Aaron Lehmann and Joshua Kwan\n"
"Usage: %s [options] dsp-file\n"
"-h\t\t\tdisplay this help screen\n"
"-p [path]\t\tmodify the plugin search path\n"
"-m [mod]\t\tchange the mod that will be used\n"
"-d [alsa|oss|wav]\tchange output driver\n"
"  -o [file|device]\tchange output dest\n"
;


int processmidi (snd_seq_t *seq_handle, thSynth *synth)
{
	snd_seq_event_t *ev;
	float *pbuf = new float[1];  // Parameter buffer
	
	do
	{
        snd_seq_event_input(seq_handle, &ev);

        switch (ev->type)
		{
			case SND_SEQ_EVENT_NOTEON:
			{
				m_sigNoteOn(ev->data.note.channel, ev->data.note.note,
							ev->data.note.velocity);

				if(ev->data.note.velocity == 0)
				{
					*pbuf = 0;
					synth->SetNoteArg(ev->data.note.channel,
									  ev->data.note.note, "trigger", pbuf, 1);
				}
				else
				{	
					synth->AddNote(ev->data.note.channel, ev->data.note.note,
								   ev->data.note.velocity);
				}
				break;
			}
			case SND_SEQ_EVENT_NOTEOFF:
			{
				m_sigNoteOff(ev->data.note.channel, ev->data.note.note);

				/* XXX make this part better */
				*pbuf = 0;
				synth->SetNoteArg(ev->data.note.channel, ev->data.note.note,
								  "trigger", pbuf, 1);
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
    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);

	return 0;
}

int main (int argc, char *argv[])
{
	string outputfname;
	string inputfname;         /* filename of .dsp file to use */
	string driver = "alsa";
	int havearg = -1;
	thALSAAudio *aout = NULL;

	/* init Glib/Gtk stuff */
	Glib::thread_init();
	gtkMain = new Gtk::Main (argc, argv);

	signal(SIGTERM, (sighandler_t)cleanup);

	plugin_path = PLUGIN_PATH;

	while ((havearg = getopt (argc, argv, "hp:o:d:")) != -1) {
		switch (havearg)
		{
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
//					printf ("error: unrecognized parameter\n");
					printf(syntax, argv[0]);
					exit(1);
				}
				break;
			}
		}
	}

	if (optind < argc) {
		inputfname = argv[optind];

		Synth.LoadMod(string(inputfname), 0, (float)12.0);
	}

	/* seed the random number generator */
	srand(time(NULL));

	/* XXX: write a config file */
	Synth.LoadMod("dsp/piano0.dsp", 1, (float)14.0);
	Synth.LoadMod("dsp/organ0.dsp", 2, (float)12.0);
	Synth.LoadMod("dsp/sqrtest.dsp", 3, (float)2.0);
	Synth.LoadMod("dsp/dfb.dsp", 4, (float)12.0);
	Synth.LoadMod("dsp/harpsi0.dsp", 5, (float)12.0);
	Synth.LoadMod("dsp/mfm01.dsp", 6, (float)12.0);
	Synth.LoadMod("dsp/analog00.dsp", 7, (float)12.0);
	Synth.LoadMod("dsp/amb01.dsp", 8, (float)12.0);
	/* drums */
	Synth.LoadMod("dsp/sd0.dsp", 9, (float)11.0);

	Glib::Thread *const ui = Glib::Thread::create(SigC::slot(&ui_thread),
												  true);

	/* all thAudio classes will work with floating point buffers converting to
	   integer internally based on format data */
	try
	{
 		if (driver == "alsa")
		{
			if (outputfname.length() > 0)
			{
				aout = new thALSAAudio(&Synth, outputfname.c_str());
			}
			else
			{
				aout = new thALSAAudio(&Synth);
			}

			/* connect our audio out event handler and bind a synth to this
			   instance */
			aout->signal_ready_write().connect(
				SigC::bind<thAudio *, thSynth *>(SigC::slot(&audio_readywrite),
												 aout, &Synth));
			aout->signal_midi_event().connect(
				SigC::bind<thSynth *>(SigC::slot(&processmidi), &Synth));
		}
		else
		{
			fprintf(stderr, "sorry, non-ALSA drivers are unsupported for now.\n");
		}

	}
	/* XXX: handle these exceptions and consolidate them to one exception
	   datatype */
	catch (thIOException e)
	{
		fprintf(stderr, "error creating audio device: %s\n", strerror(e));
		exit (1);
	}

	printf ("Using the '%s' driver\n", driver.c_str());

	if (outputfname.length() > 0)
	{
		printf ("Writing to '%s'\n", outputfname.c_str());
	}

	Synth.Process();

	while (1)
	{
		if (aout->ProcessEvents())
		{
			Synth.Process();
		}
	}

	delete aout;
	delete gtkMain;

	return 0;
}
