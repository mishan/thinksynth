# $Id: mfm01.dsp,v 1.3 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	channels=2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = env->play;

	wave1 = 5;
	wave2 = 1;
	fmamt = 0.8;

	filt1 = 1.23;
	filt2 = 1.21;
	filtlo = 0.3;
	filthi = 1;
	filtres = 0.1;

	a = 800;
	d = 2000;
	s = 0.4;	# 1 = full, 0 = off
	r = 4000;
};

node suscalc math::mul {
	in0 = ionode->velocity;
	in1 = ionode->s;
};

node env env::adsr {
	a = ionode->a;
	d = ionode->d;
	s = suscalc->out;
	r = ionode->r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->wave1;
};

node osc2 osc::simple {
	freq = freq->out;
	waveform = ionode->wave2;
	fm = osc1->out;
	fmamt = ionode->fmamt;
};

node lfo1 osc::simple {
	freq = ionode->filt1;
	waveform = 0;
};

node lfo2 osc::simple {
	freq = ionode->filt2;
	waveform = 0;
};

node lfomap1 env::map {
	in = lfo1->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node lfomap2 env::map {
	in = lfo2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node filt1 filt::moog {
	in = osc2->out;
	cutoff = lfomap1->out;
	res = ionode->filtres;
};

node filt2 filt::ink2 {
	in = filt1->out_low;
	cutoff = lfomap2->out;
	res = ionode->filtres;
};

node mixer mixer::mul {
	in0 = filt2->out;
	in1 = env->out;
};

io ionode;