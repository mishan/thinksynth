# $Id: simplefm.dsp,v 1.4 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;


	a = 1000;
	d = 1000;
	s = 0.8;
	r = 8000;

	fm_a = 2000;
	fm_d = 4000;
	fm_s = 0.8;
	fm_r = 8000;

	filt_a = 4000;
	filt_d = 10000;
	filt_s = 0.8;
	filt_r = 8000;

	filtmax = 0.9;
	filtmin = 0.1;
	res = 0.2;

	carrier_waveform = 0;
	modulator_waveform = 0;

	fmamtmin = 1;
	fmamtmax = velcalc->out;

	fmmul = 1;

	velcalcmin = 1;  # minimum modulation
	velcalcmax = 10; # maximum modulation
};

node velcalc env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->velcalcmin;
	outmax = ionode->velcalcmax;
};

node suscalc math::mul {
	in0 = ionode->velocity;
	in1 = ionode->s;
};

node suscalc_fm math::mul {
	in0 = ionode->velocity;
	in1 = ionode->fm_s;
};

node suscalc_filt math::mul {
	in0 = ionode->velocity;
	in1 = ionode->filt_s;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node fm_env env::adsr {
	a = ionode->fm_a;
	d = ionode->fm_d;
	s = suscalc_fm->out;
	r = ionode->fm_r;
	trigger = ionode->trigger;
};

node env env::adsr {
	a = ionode->a;
	d = ionode->d;
	s = suscalc->out;
	r = ionode->r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node filt_env env::adsr {
	a = ionode->filt_a;
	d = ionode->filt_d;
	s = suscalc_filt->out;
	r = ionode->filt_r;
	trigger = ionode->trigger;
};

node map1 env::map {
	in = filt_env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->filtmin;
	outmax = ionode->filtmax;
};

node map2 env::map {
	in = fm_env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fmamtmin;
	outmax = ionode->fmamtmax;
};

node filt filt::ink2 {
	in = osc1->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->carrier_waveform;
	fm = osc2->out;
	fmamt = map2->out;
};

node osc2 osc::simple {
	freq = freq->out;
	waveform = ionode->modulator_waveform;
	mul = ionode->fmmul;
};

io ionode;
