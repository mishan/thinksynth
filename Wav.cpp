#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include "endian.h"
#include "Wav.h"

Wav::Wav()
{

}

Wav::~Wav (void)
{
	close_wav();
}

int Wav::open_wav(char *name, short format, short channels, long samples, 
			  short bits)
{
	this->name = strdup(name);
	type = 1; /* writing */

	if((fd = open(name, O_CREAT|O_WRONLY, 0660)) < 0) {
		fprintf(stderr, "Wav::open: %s\n", strerror(errno));
		return -1;
	}

	fmt.format = format;
	fmt.channels = channels;
	fmt.samples = samples;
	fmt.bits = bits;

	return 0;
}

int Wav::open_wav(char *name)
{
	this->name = strdup(name);
 	type = 0; /* reading */

	if((fd = open(name, O_RDONLY)) < 0) {
		fprintf(stderr, "Wav::open: %s\n", strerror(errno));
		return -1;
	}	

	return read_header();
}

int Wav::write_wav (void *data, int len)
{
	int r = 0;

	switch(fmt.bits) {
	case 8:
		r = write(fd, ((unsigned char *)data), len);
		break;
	case 16:
	{
		int i, c;
		
		for(i = 0; i < len; i += fmt.channels) {
			for(c = 0; c < fmt.channels; c++) {
				/* if data is 16bit, then it must be signed */
				lewrite16(fd, ((signed short *)data)[c+i]);
			}
		}
	}
	}

	return 0;
}

void Wav::write_riff(void)
{
	long file_len = fmt.len + 28; /* add the fmt header size and the data header
								 size to the data length to get the file size,
								 not including the 4 bytes allocated for the
								 RIFF header */
 	long fmt_len = 16; /* this is the standard length of the fmt header,
						  it is the header minus the eight bytes for the "fmt "
						  string and the header length */
  	short format = PCM; /* PCM is assumed */
	long avgbytes = fmt.samples * fmt.channels * (fmt.bits/8); /* average bytes per sec */
	short blockalign = fmt.channels * (fmt.bits/8);

	write(fd, RIFF_HDR, 4);
 	lewrite32(fd, file_len);
	write(fd, WAVE_HDR, 4);
	write(fd, FMT_HDR, 4);
	lewrite32(fd, fmt_len);
	lewrite16(fd, format);
	lewrite16(fd, fmt.channels);
	lewrite32(fd, fmt.samples);
	lewrite32(fd, avgbytes);
	lewrite16(fd, blockalign);
	lewrite16(fd, fmt.bits);
}

void Wav::close_wav (void)
{ 
	if(type) { /* if we're writing, we must write the header before we
				  close */
		/* get our current position in the file, which is the data length */
		long data_len = lseek(fd, 0, SEEK_CUR) - 44;
		
		lseek(fd, 0, SEEK_SET);
		
		write_riff();
		
		lseek(fd, 40, SEEK_SET);
		lewrite32(fd, data_len);
	}

	close(fd);
}

int Wav::read_wav(void *data, int len)
{
	int r = -1;

	switch(fmt.bits) {
	case 8:
		r = read(fd, data, len);
		break;
	case 16:
		r = read(fd, data, len*2);
#ifdef WORDS_BIGENDIAN
		for(int i = 0; i < len; i++) {
			le16(data[i], data[i]);
		}
#endif
		break;
	default:
		fprintf(stderr, "Wav::read_wav: %s: unsupported value for bits per "
				"sample: %d\n", name, fmt.bits);
		break;
	}

	return r;
}

int Wav::read_header(void)
{
	char magic[5];
	long len;

	if(!(len = find_chunk(RIFF_HDR))) {
		fprintf(stderr, "Wav::read_header: %s: missing RIFF header\n", name);
		goto err;
	}

	read(fd, magic, 4);
	if(strncmp(WAVE_HDR, magic, 4)) {
		fprintf(stderr, "Wav::read_header: %s: missing WAVE header\n", name);
		goto err;
	}


	if(!(len = find_chunk(FMT_HDR))) {
		fprintf(stderr, "Wav::read_header: %s: could not find fmt chunk\n", name);
		goto err;
	}

	/* the fmt header must be of length 16 as we require all the information
	   it contains */
 	if(len < 16) {
		fprintf(stderr, "Wav::read_header: %s: fmt header chunk is too short\n", name);
		goto err;
	}

	/* read the fmt header into our struct */
	leread16(fd, &fmt.format);
	leread16(fd, &fmt.channels);
	leread32(fd, &fmt.samples);
	leread32(fd, &fmt.avgbytes);
	leread16(fd, &fmt.blockalign);
	leread16(fd, &fmt.bits);

	if(!(fmt.len = find_chunk(DATA_HDR))) {
		fprintf(stderr, "read_wav: %s: could not find data chunk\n", name);
		goto err;
	}

	return 0;

  err:
	close_wav();

	return -1;
}

int Wav::find_chunk(const char *label)
{
 	char magic[5];
	long len;

	for (;;) {
		if(!(read(fd, magic, 4))) {
			return 0;
		}
		leread32(fd, &len);
		if(!strncmp(label, magic, 4)) {
			break;
		}

		lseek(fd, len, SEEK_CUR);
	}

	return len;
}

Wav_Fmt Wav::get_fmt (void)
{
	return fmt;
}
