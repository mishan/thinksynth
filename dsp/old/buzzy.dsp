# $Id$
name "test";

node ionode {
	channels = 2;
	out0 = filt->out;
	out1 = filt->out;
	play = 1;

	wave = 1;
	pw = 0.7;

	mod = 2;
	rwave = 1;
	rpw = 0.2;

	rfade = 0.1;

	cutoff = 0.3;
	res = 0.5;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node modfreq math::mul {
	in0 = freq->out;
	in1 = ionode->mod;
};

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

node rosc osc::simple {
	freq = modfreq->out;
	waveform = ionode->rwave;
	reset = osc->sync;
};

node renvosc osc::simple {
	freq = freq->out;
	waveform = 1;
	pw = ionode->rpw;
	reset = osc->sync;
};

node renvmap env::map {
	in = renvosc->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = 0;
};

node rmix math::mul {
	in0 = rosc->out;
	in1 = renvmap->out;
};

node mixer mixer::fade {
	in0 = osc->out;
	in1 = rmix->out;
	fade = ionode->rfade;
};

node filt filt::res1pole {
	in = mixer->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
};

io ionode;
