# $Id: analog00.dsp,v 1.9 2004/04/22 09:20:09 ink Exp $
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = amp_env->play;

	wave = 2;
	pw = 0.4;

	cutmin = 0.1;
	cutmax = velcalc->out;
	res = 0.9;

	velcalcmin = 0;
	velcalcmax = 0.99;

	amp_a = 10000;
	amp_d = 10000;
	amp_s = 95%;
	amp_r = 20000;

	filt_a = 5000;
	filt_d = 6000;
	filt_s = 90%;
	filt_r = 1000000;
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

node filt filt::res1pole {
	in = osc1->out;
	cutoff = cutmap->out;
	res = ionode->res;
};

node filt2 filt::ink2 {
	in = filt->out;
	cutoff = cutmap->out;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

io ionode;
