
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <alsa/asoundlib.h>

#include <gtkmm.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"
#include "thMidiNote.h"

#include "thException.h"
#include "thAudio.h"
#include "thWav.h"
#include "thALSAAudio.h"
#include "thOSSAudio.h"
#include "thEndian.h"

#include "parser.h"

/* XXX: remove ALSA/OSS-specific code from libthink */

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

/* XXX: this should not be here */
snd_seq_t *open_seq (void)
{

    snd_seq_t *seq_handle;
    
    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
    {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        exit(1);
    }

    snd_seq_set_client_name(seq_handle, "thinksynth");
    
    if (snd_seq_create_simple_port(seq_handle, "thinksynth",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION) < 0)
    {
        fprintf(stderr, "Error creating sequencer port.\n");
        exit(1);
    }

    return seq_handle;
}

int processmidi (thSynth *Synth, snd_seq_t *seq_handle, float *notepitch, float *targetpitch, float *notevelocity, thMidiNote **mononote)
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
                if (ev->data.note.velocity == 0) {
                    if (*mononote && ev->data.note.note == *targetpitch) {
                        *pbuf = 0;
                        *notevelocity = 0;
                        (*mononote)->SetArg("trigger", pbuf, 1);
                        *mononote = NULL;
                    }
                }
                else
                {
                    if (*notevelocity == 0) {
                        *notevelocity = (float)ev->data.note.velocity;
                        *notepitch = (float)ev->data.note.note;
                        *targetpitch = *notepitch;
                        *mononote = Synth->AddNote(ev->data.note.channel,
                                                  *notepitch, *notevelocity);
                    }
                    else
                    {
                        *targetpitch = ev->data.note.note;
                        *notevelocity = ev->data.note.velocity;
                    }
                    return 1;
                }
                break;
            }
            case SND_SEQ_EVENT_NOTEOFF:
            {
                if (*mononote && ev->data.note.note == *targetpitch) {
                    *pbuf = 0;
                    *notevelocity = 0;
                    (*mononote)->SetArg("trigger", pbuf, 1);
                    *mononote = NULL;
                }
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
//                printf("CONTROLLER  %d\n", ev->data.control.value);
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
    string dspname = "test";   /* XXX: for debugging ONLY */
    string outputfname = "";
    string inputfname;         /* filename of .dsp file to use */
    string driver = "alsa";
    int notetoplay = 69;       /* XXX: Remove when sequencing is external */
    int samplerate = TH_SAMPLE;
    int processwindows = 100;  /* how long does sample play */
    int havearg;
    float *synthbuffer = NULL;
    thAudioFmt audiofmt;
    thAudio *outputstream = NULL;

    float startpitch;
    float notepitch;
    float targetpitch;
    int noteon;
    float monovelocity = 0;
    float monoglidemax = 0.1;      /**** this changes how fast the note slides */
    float monoglidemin = 0.001;
    float monoglide;
    thMidiNote *mononote = NULL;
    thArg *notearg;
    float *notebuffer;

    int i;
    int windowlen;

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
                if (optind != argc)
                {
//                    printf ("error: unrecognized parameter\n");
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
        Synth.AddChannel(0, dspname, 12.0);
    }

    /* seed the random number generator */
    srand(time(NULL));

    /* all thAudio classes will work with floating point buffers converting to
       integer internally based on format data */
    try
    {
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
        else /* wav */
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
    catch (thIOException e)
    {
        printf("thIOEXception on /dev/dsp\n");
        /* XXX */
    }
    catch (thWavException e)
    {
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

    while (1)
    {

        /* monophonic stuff */
        if (mononote && monovelocity) {
            mononote->SetArg("note", &notepitch, 1);
            startpitch = notepitch;
            notepitch += (targetpitch - notepitch) * monoglide;

            notearg = mononote->GetMod()->GetIONode()->GetArg("note");

            windowlen = Synth.GetWindowLen();
            notebuffer = notearg->Allocate(windowlen);
            for (i = 0; i < windowlen; i++) /* ramp the note from one window to
                                              the next, not discrete steps */
            {
                notebuffer[i] = startpitch + (i / windowlen) * (notepitch - startpitch);
            }
        }

        if (poll (pfds, seq_nfds + nfds, 1000) > 0)
        {
            int j;

            noteon = 0;
            for (j = 0; j < seq_nfds; j++)
            {
                if (pfds[j].revents > 0)
                {
                    if (noteon == 0) {
                        noteon = processmidi(&Synth, seq_handle, &notepitch, &targetpitch, &monovelocity, &mononote);
                    }
                }
            }

            monoglide = (monovelocity / MIDIVALMAX)* (monoglidemax - monoglidemin) + monoglidemin;

            /* monophonic stuff */
            if (mononote && monovelocity) {
                mononote->SetArg("note", &notepitch, 1);
                startpitch = notepitch;
                notepitch += (targetpitch - notepitch) * monoglide;
                
                notearg = mononote->GetMod()->GetIONode()->GetArg("note");
                
                windowlen = Synth.GetWindowLen();
                notebuffer = notearg->Allocate(windowlen);
                for (i = 0; i < windowlen; i++) /* ramp the note from one window to
                                                  the next, not discrete steps */
                {
                    notebuffer[i] = startpitch + (i / windowlen) * (notepitch - startpitch);
                }
            }
            
            for (j = seq_nfds; j < seq_nfds + nfds; j++)
            {
                if (pfds[j].revents > 0)
                {
                    int l = Synth.GetWindowLen();
                    Synth.Process();
                    if (outputstream->Write(synthbuffer, l) < l)
                    {
                        fprintf(stderr, "<< BUFFER UNDERRUN >> Restarting ALSA output\n");
                        snd_pcm_prepare(phandle);

                        /* this part is experimental */
                        delete outputstream;

                        if (outputfname.length() > 0)
                        {
                            outputstream = new thALSAAudio(outputfname.c_str(),
                                                           &audiofmt);
                        }
                        else
                        {
                            outputstream = new thALSAAudio(&audiofmt);
                        }

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
