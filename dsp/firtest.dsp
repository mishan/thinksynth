name "test";

node ionode {
	out0 = fir->out;
	out1 = fir->out;
	channels = 2;
	play = 1;

	waveform = 1;

	percent = 0.9;
	ilen = 32;
	imax = 1;
	pw = 0.4;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node firenv impulse::parabola {
	len = ionode->ilen;
	max = ionode->imax;
	percent = ionode->percent;
};

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	pw = ionode->pw;
};

node fir delay::fir {
	in = osc->out;
	impulse = firenv->out;
};

io ionode;
