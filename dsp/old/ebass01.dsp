name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	env_a = 1000;
	env_d = 200000;
	env_s = 100;
	env_r = 500000;

	fader1 = 0.4;
	fader2 = 0.33;

	filt_a = 12000;
	filt_d = 100000;
	filt_s = 0.1;
	filtmax = 1;
	res = 0.5;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = ionode->env_a;
	d = ionode->env_d;
	s = ionode->env_s;
	r = ionode->env_r;
	p = ionode->velocity;
	trigger = 0;
};

node cutoffmap env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 0;
	outmax = 1;
};

node filterenv env::adsr {
	a = ionode->filt_a;
	d = ionode->filt_d;
	s = ionode->filt_s;

	p = ionode->filtmax;

	trigger = 1;
};


### Time to calculate oscillator frequencies

node osc2freq math::div {  # The sub-osc
	in0 = freq->out;
	in1 = 2;
};

node osc3freq math::mul {  # One octave up
	in0 = freq->out;
	in1 = 2;
};

node filt filt::rds {
	in = oscmix2->out;
	cutoff = filterenv->out;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = 3;
};

node osc2 osc::simple {
	freq = osc2freq->out;
	waveform = 2;
	pw = 0.5;
};

node osc3 osc::simple {
	freq = osc3freq->out;
	waveform = 3;
};

node oscmix1 mixer::fade {
	in0 = osc2->out;
	in1 = osc3->out;
	fade = ionode->fader2;
};

node oscmix2 mixer::fade {
	in0 = osc1->out;
	in1 = oscmix1->out;
	fade = ionode->fader1;
};
io ionode;
