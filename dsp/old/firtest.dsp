# $Id$
name "test";

node ionode {
	out0 = fir->out;
	out1 = fir->out;
	channels = 2;
	play = 1;

	waveform = 1;
	pw = 0.2;

	ilen = 64;
	fnum = 10;
	width = 10;
	firmix = 0.4;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node firenv impulse::square {
	len = ionode->ilen;
	num = ionode->fnum;
	width = ionode->width;
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

io ionode;
