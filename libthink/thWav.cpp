/* $Id: thWav.cpp,v 1.37 2003/09/26 07:33:12 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef USING_FREEBSD	/* FreeBSD has endian.h elsewhere */
# include <machine/endian.h>
#else
#  ifdef USING_CYGWIN
#   include <asm/byteorder.h> /* Cygwin on Win32 */
#  else
#   include <endian.h>	/* Linux and a lot of other things */
#  endif
#endif

#include "thEndian.h"
#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thOSSAudio.h"
#include "thWav.h"
// added on 9/15/03 by brandon
#include "think.h"

thWav::thWav(char *name)
	throw(thIOException, thWavException)
{
	filename = strdup(name);
	type = READING;

	if(!(file = fopen(filename, "r"))) {
		throw (thIOException) errno;
	}
	// added by Brandon on 9/15/03
	outbuf=NULL;
	ReadHeader();
}

thWav::thWav(char *name, const thAudioFmt *wfmt)
	throw(thIOException)
{
	filename = strdup(name);
	type = WRITING; /* writing */

	if((fd = open(name, O_CREAT|O_WRONLY|O_TRUNC, 0660)) < 0) {
		throw (thIOException)errno;
	}

	/* average bytes per sec */
	avgbytes = wfmt->samples * wfmt->channels * (wfmt->bits/8);

	blockalign = wfmt->channels * (wfmt->bits/8);

	memcpy(&fmt, wfmt, sizeof(thAudioFmt));

	/* XXX: support other formats */
	fmt.format = PCM;

	lseek(fd, 44, SEEK_SET);
	// added by brandon on 9/15/03

	outbuf=NULL;
}

thWav::~thWav (void)
{
	if(type == WRITING) { /* if we're writing, we must write the header before
							 we close */
		/* get our current position in the file, which is the data length */
		fmt.len = lseek(fd, 0, SEEK_CUR);
		WriteRiff();
		close(fd);
	}
	else {
		fclose(file);
	}
	if(filename) {
		free(filename);
	}
	if (outbuf) free(outbuf);
}

//* changed this function on 9/15/03
int thWav::Write (float *inbuf, int len)
{
	int r = -1;
	// added this
	int i;
	if(type == READING) {
		/* XXX: throw an exception */
		return -1;
	}
	// added this
	// it would be *bad* if len were to increase
	// between calls
	if (!outbuf){
		outbuf = malloc (len*fmt.bits);
		if (!outbuf){
			fprintf(stderr,"thWav::Write -- can't allocate output buffer");
		}
	}
	// changed this so as to perform a conversion
	// on the specified buffer
	if ( fmt.bits == 8){
		unsigned char *data=(unsigned char*)outbuf;
		for (i=0; i < len; i++){
			data[i]=(unsigned char)(((float)inbuf[i]/TH_MAX)*128);
		}
		r = write(fd, data, len);
	}
	else if (fmt.bits == 16){
		unsigned short *data=(unsigned short*)outbuf;
		for (i=0; i < len; i++){
			le16(data[i],(signed short)(((float)inbuf[i]/TH_MAX)*32767));
		}
		r=write(fd, data, len*sizeof(short));
	}
	else {
		fprintf(stderr, "thWav::Write -- %d-bit audio is unsupported\n",fmt.bits);
	}
	return r;
}

/* Alters file seekage */
void thWav::WriteRiff (void)
{
	lseek(fd, 0, SEEK_SET);

	int fmt_len = FMT_LEN; /* this is the standard length of the fmt header,
							   it is the header minus the eight bytes for the 
							   "fmt " string and the header length */
	

	write(fd, RIFF_HDR, 4);
	lewrite32(fd, fmt.len - 8);
	write(fd, WAVE_HDR, 4);
	write(fd, FMT_HDR, 4);
	lewrite32(fd, fmt_len);
	lewrite16(fd, fmt.format);
	lewrite16(fd, fmt.channels);
	lewrite32(fd, fmt.samples);
	lewrite32(fd, avgbytes);
	lewrite16(fd, blockalign);
	lewrite16(fd, fmt.bits);

	write(fd, DATA_HDR, 4);
	lewrite32(fd, fmt.len - 44);
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
	{
		r = fread(data, sizeof(signed short), len, file);
#ifdef WORDS_BIGENDIAN
		for(int i = 0; i < r; i++) {
			le16(((signed short *)data)[i], ((signed short *)data)[i]);
		}
#endif
	}
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

int thWav::FindChunk (const char *label) const
{
 	char magic[5];
	int len;

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

thAudioFmt *thWav::GetFormat (void)
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
