#ifndef HAVE_WAV_H
#define HAVE_WAV_H 1

/* WAV headers */
#define RIFF_HDR "RIFF"
#define WAVE_HDR "WAVE"
#define FMT_HDR "fmt "
#define DATA_HDR "data"

/* formats */
#define PCM 1

typedef struct {
	long samples;
	long avgbytes;
	long len;

	short blockalign;
	short bits;
	short format;
	short channels;
} Wav_Fmt;

class Wav {
public:
	/* constructor - doesn't do anything */
	Wav();

	/* our deconstructor, should close fd if it hasn't been closed */
	~Wav();

	/* overloaded open method for reading files */
	int open_wav(char *name);

	/* overloaded open method for writing files */
	int open_wav(char *name, short format, short channels, long samples, 
			 short bits);

 	int write_wav(void *data, int len);
	int read_wav(void *data, int len);

	/* this is necessary only for writing files as the header must be 
	   written */
	void close_wav (void);

	/* for debugging purposes only */
	void print_info (void);

	Wav_Fmt get_fmt (void);
private:
	char *name;
	int fd;
	bool type; /* 0 for reading, 1 for writing */
	Wav_Fmt fmt;

	void write_riff (void);
	int find_chunk(const char *label);
	int read_header (void);
};

#endif /* HAVE_WAV_H */
