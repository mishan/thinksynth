# $Id: analog02.dsp,v 1.1 2004/02/19 00:38:29 ink Exp $
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = amp_env->play;

	wave = 1;
	pw = 0.4;

	cutmin = 0.1;
	cutmax = velcalc->out;
	res = 0.9;

	hcutmin = 800;
	hcutmax = 2000;
	hres = 0.9;

	velcalcmin = 0.3;
	velcalcmax = 0.99;

	amp_a = 2000;
	amp_d = 10000;
	amp_s = 100;
	amp_r = 8000;

	filt_a = 4000;
	filt_d = 6000;
	filt_s = 60;
	filt_r = 100000;

	h_filt_a = 10000;  # for the high pass filter
	h_filt_d = 6000;
	h_filt_s = 90;
	h_filt_r = 50000;

	hpmix = 0.6;  # how much of the high-passed signal is mixed in before
	              # the low-pass
};

node velcalc env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->velcalcmin;
	outmax = ionode->velcalcmax;
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
	in = osc1->out;
	cutoff = hcutmap->out;
	res = ionode->hres;
};

node hpmix mixer::fade {
	in0 = osc1->out;
	in1 = hfilt->out_high;
	fade = ionode->hpmix;
};

node filt2 filt::ink2 {
	in = hpmix->out;
	cutoff = cutmap->out;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

io ionode;
