#ifndef TH_WAV_H
#define TH_WAV_H 1

/* WAV headers */
#define RIFF_HDR "RIFF"
#define WAVE_HDR "WAVE"
#define FMT_HDR "fmt "
#define DATA_HDR "data"
#define FMT_LEN 16

/* formats */
#define PCM 1


enum thWavException { NORIFFHDR, NOWAVHDR, NOFMTHDR, BADFMTHDR, NODATA };

enum thWavType { READING, WRITING };

inline char *thWavError(thWavException e)
{
	switch(e) {
	case NORIFFHDR:
		return "No RIFF header found";
		break;
	case NOWAVHDR:
		return "No WAVE header found";
		break;
	case NOFMTHDR:
		return "No fmt header found";
		break;
	case BADFMTHDR:
		return "Bad fmt header";
		break;
	case NODATA:
		return "No data header found";
		break;
	default:
		return "Unhandled Wav exception";
		break;
	}

	return NULL;
}

class thWav: public thAudio
{
public:
	/* constructor for reading files */
	thWav(char *name)
		throw(thIOException, thWavException);

	/* constructor for writing files */
	thWav(char *name, const thAudioFmt *fmt)
		throw(thIOException);

	/* our deconstructor, should close fd */
	virtual ~thWav();

 	int Write(void *data, int len);
	int Read(void *data, int len);

	const thAudioFmt *GetFormat (void);
	thWavType GetType (void);

	void SetFormat(const thAudioFmt *wfmt);
private:
	char *filename; /* path to the file */
	int fd;
	FILE *file;
	thWavType type;
	thAudioFmt fmt;

	short blockalign; /* wav-specific info */
	long avgbytes;

	void WriteRiff (void);
	int FindChunk (const char *label);
	void ReadHeader (void)
		throw(thWavException);
};

inline thWav *new_thWav(char *name)
{
	try {
		thWav *wav = new thWav(name);

		return wav;
	}
	catch (thIOException e) {
		fprintf(stderr, "thWav::thWav: %s: %s\n", name, strerror(e));	
	}
	catch (thWavException e) {
		fprintf(stderr, "%s\n", thWavError(e));
	}

	return NULL;
}

inline thWav *new_thWav(char *name, thAudioFmt *wfmt)
{
	try {
		thWav *wav = new thWav(name, wfmt);

		return wav;
	}
	catch (thIOException e) {
		fprintf(stderr, "thWav::thWav: %s: %s\n", name, strerror(e));	
	}

	return NULL;
}

#endif /* TH_WAV_H */
