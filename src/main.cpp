/* $Id: main.cpp,v 1.125 2004/02/01 09:23:31 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <alsa/asoundlib.h>

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

string plugin_path;

const char syntax[] = \
PACKAGE_NAME " " PACKAGE_VERSION " by Leif M. Ames, Misha Nasledov, "
"Aaron Lehmann and Joshua Kwan\n"
"Usage: %s [options] dsp-file\n"
"-h\t\tdisplay this help screen\n"
"-p [path]\tmodify the plugin search path\n"
"-m [mod]\tchange the mod that will be used\n";

snd_seq_t *open_seq();
int processmidi(thSynth *synth, snd_seq_t *seq_handle);

int main (int argc, char *argv[])
{
	string dspname = "test";   /* XXX: for debugging ONLY */
	string outputfname = "";
	string inputfname;         /* filename of .dsp file to use */
	string driver = "alsa";
	int notetoplay = 69;       /* XXX: Remove when sequencing is external */
	int samplerate = TH_SAMPLE;
	int processwindows = 100;  /* how long does sample play */
	int buflen, havearg, i;
	float *synthbuffer = NULL;
	thAudioFmt audiofmt;
	thAudio *outputstream = NULL;

	snd_seq_t *seq_handle;    /* for ALSA midi */
	int seq_nfds, nfds;
	struct pollfd *pfds;
	snd_pcm_t *phandle;

	plugin_path = PLUGIN_PATH;

	while ((havearg = getopt (argc, argv, "hp:m:n:l:o:s:d:")) != -1) {
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
			case 's':
			{
				printf("changing sample rate\n");
				samplerate = atoi(optarg);
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
			case 'm':
			{
				dspname = optarg;
				break;
			}
			case 'n':  /* XXX: TAKE THIS OUT WHEN SEQUENCING IS EXTERNAL */
			{
				notetoplay = atoi(optarg);
				break;
			}
			case 'l':  /* number of windows to process */
			{
				processwindows = atoi(optarg);
				break;
			}
			default:
			{
				if (optind != argc) {
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
		Synth.LoadMod(inputfname);

		/* the first channel is the one passed on the command line */
		Synth.AddChannel(string("chan0"), dspname, 12.0);
	}

	/* seed the random number generator */
	srand(time(NULL));

	Synth.LoadMod("dsp/piano0.dsp");
	Synth.AddChannel(string("chan1"), "test", 14.0);
	Synth.LoadMod("dsp/organ0.dsp");
	Synth.AddChannel(string("chan2"), "test", 12.0);
	Synth.LoadMod("dsp/sqrtest.dsp");
	Synth.AddChannel(string("chan3"), "test", 2.0);

	Synth.LoadMod("dsp/dfb.dsp");
	Synth.AddChannel(string("chan4"), "test", 12.0);
	Synth.LoadMod("dsp/harpsi0.dsp");
	Synth.AddChannel(string("chan5"), "test", 12.0);
	Synth.LoadMod("dsp/harpsi1.dsp");
	Synth.AddChannel(string("chan5"), "test", 12.0);
	

	/* drums */
	Synth.LoadMod("dsp/sd0.dsp");
	Synth.AddChannel(string("chan9"), "test", 11.0);

//	Synth.AddNote(string("chan1"), notetoplay, TH_MAX);

	/* all thAudio classes will work with floating point buffers converting to
	   integer internally based on format data */
	try {
		/* XXX: note that this is actually bad since there are potentially
		   other /dev/dsp devices */
		audiofmt.channels = Synth.GetChans();
		audiofmt.bits = 16;
		audiofmt.samples = samplerate;

		if (driver == "alsa")
		{
			audiofmt.period = Synth.GetWindowLen();

			if (outputfname.length() > 0)
			{
				outputstream = new thALSAAudio(outputfname.c_str(), &audiofmt);
			}
			else
			{
				outputstream = new thALSAAudio(&audiofmt);
			}

			phandle = ((thALSAAudio *)outputstream)->play_handle;

			seq_handle = open_seq();
			seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
			nfds = snd_pcm_poll_descriptors_count (phandle);
			pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * 
										   (seq_nfds + nfds));
			snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);
			snd_pcm_poll_descriptors (phandle, pfds+seq_nfds, nfds);

		}
		else if (driver == "oss")
		{
			fprintf(stderr, "%s:%d: sorry, OSS is broken for now\n", __FILE__,
					__LINE__);
			exit (1);
			outputstream = new thOSSAudio(NULL, &audiofmt);
		}
		else
		{
			if (outputfname.length() > 0)
			{
				outputstream = new thWav((char *)outputfname.c_str(),
										 &audiofmt);
			}
			else
			{
				outputstream = new thWav("test.wav", &audiofmt);

			}
		}
	}

	/* XXX: handle these exceptions and consolidate them to one exception
	   datatype */
	catch (thIOException e) {
		printf("thIOEXception on /dev/dsp\n");
		/* XXX */
	}
	catch (thWavException e) {
		printf("thWavException on %s\n", outputfname.c_str());
		/* XXX */
	}

	/* grab address of buffer from synth object */
	synthbuffer = Synth.GetOutput();

	printf ("Using the '%s' driver\n", driver.c_str());
	if (outputfname.length() > 0)
	{
		printf ("Writing to '%s'\n", outputfname.c_str());
	}

	while (1) {
//	for (i = 0; i < processwindows; i++) {
		if (poll (pfds, seq_nfds + nfds, 1000) > 0) {
			int j;

			for (j = 0; j < seq_nfds; j++)
			{
				if(pfds[j].revents > 0)
				{
					processmidi(&Synth, seq_handle);
				}
			}
			for(j = seq_nfds; j < seq_nfds + nfds; j++)
			{
				if (pfds[j].revents > 0)
				{
					int l = Synth.GetWindowLen();
					Synth.Process();
					if(outputstream->Write(synthbuffer, l) < l) {
						fprintf(stderr, "<< BUFFER UNDERRUN >>   Restarting ALSA output\n");
						snd_pcm_prepare(phandle);

						/* this part is experimental */
						snd_pcm_close(phandle);
						delete outputstream;
						outputstream = new thALSAAudio(outputfname.c_str(), &audiofmt);
						phandle = ((thALSAAudio *)outputstream)->play_handle;
						//nfds = snd_pcm_poll_descriptors_count (phandle);
						//snd_pcm_poll_descriptors (phandle, pfds+seq_nfds, nfds);
					}
				}
			}
		}
	}

	delete outputstream;

	return 0;
}

snd_seq_t *open_seq() {

    snd_seq_t *seq_handle;
    
    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        exit(1);
    }
    snd_seq_set_client_name(seq_handle, "thinksynth");
    if (snd_seq_create_simple_port(seq_handle, "thinksynth",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
        fprintf(stderr, "Error creating sequencer port.\n");
        exit(1);
    }
    return(seq_handle);
}

int processmidi(thSynth *Synth, snd_seq_t *seq_handle)
{
	snd_seq_event_t *ev;
	float *pbuf = new float[1];  // Parameter buffer
	char channelname[10];  /* XXX: FOR HARDCODED CHANNELS  remove this later */
	
	do {
        snd_seq_event_input(seq_handle, &ev);
		sprintf(channelname, "chan%i", ev->data.note.channel);
        switch (ev->type) {
		case SND_SEQ_EVENT_NOTEON:
			Synth->AddNote(string(channelname), ev->data.note.note, ev->data.note.velocity);
			break;
		case SND_SEQ_EVENT_NOTEOFF:
			*pbuf = 0;
			Synth->SetNoteArg(string(channelname), ev->data.note.note, "trigger", pbuf, 1);  /* XXX make this part better */
			break;
		case SND_SEQ_EVENT_TEMPO:
			printf("TEMPO CHANGE:  %i\n", ev->data.control.value);
			break;
		case SND_SEQ_EVENT_TICK:
			printf("TICK CHANGE:  %i\n", ev->data.control.value);
			break;
		case SND_SEQ_EVENT_PITCHBEND:
			printf("PITCH BEND:  %i\n", ev->data.control.value);
			break;
		}
		snd_seq_free_event(ev);
    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
    return (0);
}
