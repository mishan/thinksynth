/* $Id */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include "thEndian.h"
#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thOSSAudio.h"
#include "thWav.h"

thWav::thWav(char *name)
	throw(thIOException, thWavException)
{
	filename = strdup(name);
	type = READING;

	if(!(file = fopen(filename, "r"))) {
		throw (thIOException) errno;
	}

	ReadHeader();
}

thWav::thWav(char *name, const thAudioFmt *wfmt)
	throw(thIOException)
{
	filename = strdup(name);
	type = WRITING; /* writing */

	if((fd = open(name, O_CREAT|O_WRONLY, 0660)) < 0) {
		throw (thIOException)errno;
	}

	/* average bytes per sec */
	avgbytes = wfmt->samples * wfmt->channels * (wfmt->bits/8);
	
	blockalign = wfmt->channels * (wfmt->bits/8);
	
	memcpy(&fmt, wfmt, sizeof(thAudioFmt));

	/* XXX: support other formats */
	fmt.format = PCM;
}

thWav::~thWav (void)
{
	if(type == WRITING) { /* if we're writing, we must write the header before 
							 we close */
		/* get our current position in the file, which is the data length */
		long data_len = lseek(fd, 0, SEEK_CUR) - 44;
		
		lseek(fd, 0, SEEK_SET);
		
		WriteRiff();
		
		lseek(fd, 40, SEEK_SET);
		lewrite32(fd, data_len);

		close(fd);
	}
	else {
		fclose(file);
	}

}

int thWav::Write (void *data, int len)
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

void thWav::WriteRiff (void)
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
	lewrite32(fd, avgbytes);
	lewrite16(fd, blockalign);
	lewrite16(fd, fmt.bits);
}

int thWav::Read (void *data, int len)
{
	int r = -1;

	if(type == WRITING) {
		/* XXX: throw an exception */
		return -1;
	}
	
	switch(fmt.bits) {
	case 8:
		r = fread(data, sizeof(unsigned char), len, file);
		break;
	case 16:
		r = fread(data, sizeof(signed short), len, file);
#ifdef WORDS_BIGENDIAN
		for(int i = 0; i < r; i++) {
			/* XXX */
			signed short _data;

			memcpy(&_data, &((signed short *)data)[i], sizeof(signed short));

			le16(_data, _data);

			memcpy(&((signed short *)data)[i], &_data, sizeof(signed short));
		}
#endif
		break;
	default:
		fprintf(stderr, "thWav::Read: %s: unsupported value for bits per "
				"sample: %d\n", filename, fmt.bits);
		break;
	}

	return r;
}

void thWav::ReadHeader (void)
	throw(thWavException)
{
	char magic[5];
	long len;

	if(!(len = FindChunk(RIFF_HDR))) {
		throw NORIFFHDR;
	}

	fread(magic, 4, 1, file);
	if(strncmp(WAVE_HDR, magic, 4)) {
		throw NOWAVHDR;

	}

	if(!(len = FindChunk(FMT_HDR))) {
		throw NOFMTHDR;
	}

	/* the fmt header must be of length 16 as we require all the information
	   it contains */
 	if(len < 16) {
		throw BADFMTHDR;
	}

	/* read the fmt header into our struct */
	lefread16(file, &fmt.format);
	lefread16(file, &fmt.channels);
	lefread32(file, &fmt.samples);
	lefread32(file, &avgbytes);
	lefread16(file, &blockalign);
	lefread16(file, &fmt.bits);

	if(!(fmt.len = FindChunk(DATA_HDR))) {
		throw NODATA;
	}
}

int thWav::FindChunk (const char *label)
{
 	char magic[5];
	long len;

	for (;;) {
		if(!(fread(magic, 1, 4, file))) {
			return 0;
		}
		lefread32(file, &len);
		if(!strncmp(label, magic, 4)) {
			break;
		}

		fseek(file, len, SEEK_CUR);
	}

	return len;
}

const thAudioFmt *thWav::GetFormat (void)
{
	thAudioFmt *afmt = new thAudioFmt;

	memcpy(afmt, &fmt, sizeof(thAudioFmt));

	return afmt;
}

void thWav::SetFormat (const thAudioFmt *wfmt)
{
	if(type == READING) {
		return;
	}

	memcpy(&fmt, wfmt, sizeof(thAudioFmt));
}
