name "test";

node ionode {
	out0 = fir->out;
	out1 = fir->out;
	channels = 2;
	play = 1;

	waveform = 1;

	percent = 1;
	ilen = 64;
	cutoff = 0.7;
	firmix = 0.2;
	pw = 0.1;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node firenv impulse::blackman {
	len = ionode->ilen;
	cutoff = ionode->cutoff;
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
