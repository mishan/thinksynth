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
#include "Exception.h"
#include "Wav.h"

Wav::Wav(char *name)
	throw(IOException, WavException)
{

	filename = strdup(name);
 	type = READING; /* reading */

	if((fd = open(filename, O_RDONLY)) < 0) {
		throw (IOException)errno;
	}

	try {
		read_header();
	}
	catch (WavException e) {
		throw e;
	}
}

Wav::Wav(char *name, WavFormat *wfmt)
	throw(IOException)
{
	filename = strdup(name);
	type = WRITING; /* writing */

	if((fd = open(name, O_CREAT|O_WRONLY, 0660)) < 0) {
		throw (IOException)errno;
	}

	/* average bytes per sec */
	wfmt->avgbytes = wfmt->samples * wfmt->channels * (wfmt->bits/8);

	wfmt->blockalign = wfmt->channels * (wfmt->bits/8);
	
	/* XXX: support other formats */
	wfmt->format = PCM;

	memcpy(&fmt, wfmt, sizeof(WavFormat));
}

Wav::~Wav (void)
{
	if(type == WRITING) { /* if we're writing, we must write the header before 
							 we close */
		/* get our current position in the file, which is the data length */
		long data_len = lseek(fd, 0, SEEK_CUR) - 44;
		
		lseek(fd, 0, SEEK_SET);
		
		write_riff();
		
		lseek(fd, 40, SEEK_SET);
		lewrite32(fd, data_len);
	}
	
	close(fd);
}

int Wav::write_wav (void *data, int len)
{
	int r = -1;

	if(type == READING) {
		/* XXX: throw an exception */
		return -1;
	}

	switch(fmt.bits) {
	case 8:
		/* if data is 8bit, it must be unsigned */
		r = write(fd, ((unsigned char *)data), len);
		break;
	case 16:
	{
		int i, c, l;
		
		for(i = 0; i < len; i += fmt.channels) {
			for(c = 0; c < fmt.channels; c++) {
				/* if data is 16bit, then it must be signed */
				if((l = lewrite16(fd, ((signed short *)data)[c+i])) < 0) {
					/* XXX: handle error */
				}
				else { r += l; }
			}
		}
	}
	}

	return r;
}

void Wav::write_riff(void)
{
	long file_len = fmt.len + 28; /* add the fmt header size and the data 
									 header size to the data length to get the 
									 file size, not including the 4 bytes 
									 allocated for the RIFF header */
 	long fmt_len = FMT_LEN; /* this is the standard length of the fmt header,
							   it is the header minus the eight bytes for the 
							   "fmt " string and the header length */

	write(fd, RIFF_HDR, 4);
 	lewrite32(fd, file_len);
	write(fd, WAVE_HDR, 4);
	write(fd, FMT_HDR, 4);
	lewrite32(fd, fmt_len);
	lewrite16(fd, fmt.format);
	lewrite16(fd, fmt.channels);
	lewrite32(fd, fmt.samples);
	lewrite32(fd, fmt.avgbytes);
	lewrite16(fd, fmt.blockalign);
	lewrite16(fd, fmt.bits);
}


int Wav::read_wav(void *data, int len)
{
	int r = -1;

	if(type == WRITING) {
		/* XXX: throw an exception */
		return -1;
	}
	
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
				"sample: %d\n", filename, fmt.bits);
		break;
	}

	return r;
}

void Wav::read_header(void)
	throw(WavException)
{
	char magic[5];
	long len;

	if(!(len = find_chunk(RIFF_HDR))) {
		throw NORIFFHDR;
	}

	read(fd, magic, 4);
	if(strncmp(WAVE_HDR, magic, 4)) {
		throw NOWAVHDR;

	}

	if(!(len = find_chunk(FMT_HDR))) {
		throw NOFMTHDR;
	}

	/* the fmt header must be of length 16 as we require all the information
	   it contains */
 	if(len < 16) {
		throw BADFMTHDR;
	}

	/* read the fmt header into our struct */
	leread16(fd, &fmt.format);
	leread16(fd, &fmt.channels);
	leread32(fd, &fmt.samples);
	leread32(fd, &fmt.avgbytes);
	leread16(fd, &fmt.blockalign);
	leread16(fd, &fmt.bits);

	if(!(fmt.len = find_chunk(DATA_HDR))) {
		throw NODATA;
	}
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

WavFormat Wav::get_format (void)
{
	return fmt;
}

WavType Wav::get_type (void)
{
	return type;
}
