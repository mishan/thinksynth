# Piano-like synth
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	a = 0;
	d = 6000;
	s = 130;
	r = 12000;

	sfreqlo = 0;
	sfreqhi = 3000;

	osc2detune = 0.1;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node detune math::add {
	in0 = freq->out;
	in1 = ionode->osc2detune;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = ionode->a;
	d = ionode->d;
	s = ionode->s;
	r = ionode->r;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 0;
	outmax = 1;
};

node map2 env::map {
	in = env->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->sfreqlo;
	outmax = ionode->sfreqhi;
};

node filt filt::rds {
	in = mixer2->out;
	cutoff = map1->out;
	res = 0.4;
};

node osc osc::softsqr {
	freq = freq->out;
	sfreq = map2->out;
	pw = 0.3;
};

node osc2 osc::softsqr {
	freq = detune->out;
	sfreq = map2->out;
	pw = 0.55;
};

node mixer2 mixer::add {
	in0 = osc->out;
	in1 = osc2->out;
};

io ionode;
