name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = amp_env->play;

	wave = 2;
	pw = 0.2;
	subwave = 2;
	subpw = 0.3;
	sublevel = 0.2;

	cutmin = 40;
	cutmax = 3000;
	res = 0.9;

	amp_a = 4000;
	amp_d = 30000;
	amp_s = 130;
	amp_r = 20000;

	filt_a = 10000;
	filt_d = 20000;
	filt_s = 100;
	filt_r = 20000;

	trigger = 0;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node subfreq math::div {
	in0 = freq->out_band;
	in1 = 2;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = amp_env->out;
};

node amp_env env::adsr {
	a = ionode->amp_a;
	d = ionode->amp_d;
	s = ionode->amp_s;
	r = ionode->amp_r;
	trigger = ionode->trigger;
};

node filt_env env::adsr {
	a = ionode->filt_a;
	d = ionode->filt_d;
	s = ionode->filt_s;
	r = ionode->filt_r;
	trigger = ionode->trigger;
};

node cutmap env::map {
	in = filt_env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
};

node filt filt::res2pole {
	in = mixer2->out;
	cutoff = cutmap->out;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

node osc2 osc::simple {
	freq = subfreq->out;
	waveform = ionode->subwave;
	pw = ionode->subpw;
};

node mixer2 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->sublevel;
};

io ionode;
