# $Id: coolbuzz.dsp,v 1.2 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = 1;

	waveform = 1;
	pw = 0.2;

	ilen = 128;
	fnum = 5;
	fpw = 0.2;
	firmix = 0.3;

	cutoff = 0.8;
	res = 0.4;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node firenv impulse::square {
	len = ionode->ilen;
	num = ionode->fnum;
	pw = ionode->fpw;
};

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	pw = ionode->pw;
};

node fir delay::fir {
	in = osc->out;
	impulse = firenv->out;
	mix = ionode->firmix;
};

node filt filt::ink {
	in = fir->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
};

io ionode;
