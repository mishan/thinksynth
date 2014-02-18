
#ifndef TH_OSSAUDIO_H
#define TH_OSSAUDIO_H

#include <sys/soundcard.h>

#define DEFAULT_FREQ 44100
#define DEFAULT_FORMAT AFMT_U8
#define DEFAULT_CHANNELS 2
#define DEFAULT_SAMPLES 4096

#define BUF_SIZE 65536
#define BUF_REFILL_PERCENT 0.5

class thOSSAudio: public thAudio
{
public:
    thOSSAudio(char *null, const thAudioFmt *afmt)
        throw(thIOException);

    virtual ~thOSSAudio();

    void Play (thAudio *audioPtr);
    // changed on 9/15/03 by brandon lewis
    // all thAudio classes will work with floating point buffers
    // converting to integer internally based on format data
    int Write (float *, int len);
    int Read (void *, int len);
    const thAudioFmt *GetFormat (void) { return &ofmt; };
    void SetFormat (const thAudioFmt *fmt);

    bool ProcessEvents (void);
private:
    int fd;
    thAudioFmt ofmt;
    // added second format structure on 9/15/03
    // intended to store the actual format of the synth data
    thAudioFmt ifmt;
    void *outbuf;
};

inline thOSSAudio *new_thOSSAudio(char *null, const thAudioFmt *afmt)
{
    try {
        thOSSAudio *audio = new thOSSAudio(null, afmt);

        return audio;
    }
    catch (thIOException e) {
        fprintf(stderr, "thOSSAudio::thOSSAudio: %s\n", strerror(e));    
    }

    return NULL;
}

#endif /* TH_OSSAUDIO_H */
