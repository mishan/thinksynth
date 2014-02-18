name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = amp_env->play;

	factor = velcalc2->out;
	factor2 = velcalc2->out;

	cutmin = 0.08;
	cutmax = velcalc->out;
	res = 0.2;

	hcutmin = 0;
	hcutmax = 5000;
	hres = 0.5;

	velcalcmin = 0.1;
	velcalcmax = 0.9;
	velcalc2min = 30;
	velcalc2max = 1;

	amp_a = 0;
	amp_d = 1000;
	amp_s = 100;
	amp_r = 8000;

	filt_a = 0;
	filt_d = 200000;
	filt_s = 50;
	filt_r = 100000;

	h_filt_a = 8000;  # for the high pass filter
	h_filt_d = 50000;
	h_filt_s = 40;
	h_filt_r = 50000;

	hpmix = 0.4;  # how much of the high-passed signal is mixed in before
	              # the low-pass
	osc2mul = 2;
	oscfade = 0.3;
};

node velcalc env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->velcalcmin;
	outmax = ionode->velcalcmax;
};

node velcalc2 env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->velcalc2min;
	outmax = ionode->velcalc2max;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt2->out;
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

node hfilt_env env::adsr {
	a = ionode->h_filt_a;
	d = ionode->h_filt_d;
	s = ionode->h_filt_s;
	r = ionode->h_filt_r;
	trigger = ionode->trigger;
};

node cutmap env::map {
	in = filt_env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
};

node hcutmap env::map {
	in = hfilt_env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->hcutmin;
	outmax = ionode->hcutmax;
};

node hfilt filt::res2pole {
	in = premix->out;
	cutoff = hcutmap->out;
	res = ionode->hres;
};

node hpmix mixer::fade {
	in0 = premix->out;
	in1 = hfilt->out_high;
	fade = ionode->hpmix;
};

node filt2 filt::ink2 {
	in = hpmix->out;
	cutoff = cutmap->out;
	res = ionode->res;
};

node osc1 osc::shapeo {
	freq = freq->out;
	shape = ionode->factor;
};

node osc2mul math::mul {
	in0 = freq->out;
	in1 = ionode->osc2mul;
};

node osc2 osc::buzzer {
	freq = osc2mul->out;
	factor = ionode->factor2;
};

node premix mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->oscfade;
};

io ionode;
