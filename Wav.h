#ifndef HAVE_WAV_H
#define HAVE_WAV_H 1

/* WAV headers */
#define RIFF_HDR "RIFF"
#define WAVE_HDR "WAVE"
#define FMT_HDR "fmt "
#define DATA_HDR "data"
#define FMT_LEN 16

/* formats */
#define PCM 1

struct WavFormat {
	long samples;
	long avgbytes;
	long len;

	short blockalign;
	short bits;
	short format;
	short channels;
};

enum WavException { NORIFFHDR, NOWAVHDR, NOFMTHDR, BADFMTHDR, NODATA };

enum WavType { READING, WRITING };

inline char *WavError(WavException e)
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

class Wav: public Audio
{
public:
	/* constructor for reading files */
	Wav(char *name)
		throw(IOException, WavException);

	/* constructor for writing files */
	Wav(char *name, WavFormat *fmt)
		throw(IOException);

	/* our deconstructor, should close fd */
	virtual ~Wav();

 	int write_wav(void *data, int len);
	int read_wav(void *data, int len);
	int Read(void *data, int len);

	WavFormat get_format (void);
	WavType get_type (void);
private:
	char *filename; /* path to the file */
	int fd;
	WavType type;
	WavFormat fmt;

	void write_riff (void);
	int find_chunk(const char *label);
	void read_header (void)
		throw(WavException);
};

inline Wav *new_Wav(char *name)
{
	try {
		Wav *wav = new Wav(name);

		return wav;
	}
	catch (IOException e) {
		fprintf(stderr, "Wav::Wav: %s: %s\n", name, strerror(e));	
	}
	catch (WavException e) {
		fprintf(stderr, "%s\n", WavError(e));
	}

	return NULL;
}

inline Wav *new_Wav(char *name, WavFormat *wfmt)
{
	try {
		Wav *wav = new Wav(name, wfmt);

		return wav;
	}
	catch (IOException e) {
		fprintf(stderr, "Wav::Wav: %s: %s\n", name, strerror(e));	
	}

	return NULL;
}

#endif /* HAVE_WAV_H */
