# $Id$
# glitch synth 1
# leif ames <ink@bespin.org>
# 5-30-2004

name "test";

node ionode {
	channels=2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = env->play;

	wave1 = 5;
	wave2 = 1;
	fmamt = 0.8;

	lfolow = 0.01;	# at 0
	lfohigh = 30;	# at 88
	lfo2low = 0.01;
	lfo2high = 20;
	filtlo = 100;
	filthi = 1000;
	filtres = 6;
	lpfcut = 0.6;
	lpfres = 0.02;

	a = 1000ms;
	d = 0ms;
	s = 100%;	# 1 = full, 0 = off
	r = 1000ms;
};

node env env::adsr {
	a = ionode->a;
	d = ionode->d;
	s = ionode->s;
	r = ionode->r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::static {
};


node lfo1calc env::map {
	in = ionode->note;
	inmin = 0;
	inmax = 88;
	outmin = ionode->lfolow;
	outmax = ionode->lfohigh;
};

node lfo2calc env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->lfo2low;
	outmax = ionode->lfo2high;
};

node lfo1 osc::simple {
	freq = lfo1calc->out;
	waveform = 3;
};

node lfo2 osc::simple {
	freq = lfo2calc->out;
	waveform = 3;
};

node lfomix mixer::mul {
	in0 = lfo2->out;
	in1 = lfo1->out;
};

node lfomap1 env::map {
	in = lfomix->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node filt1 filt::res2pole2 {
	in = filt2->out;
	cutoff = lfomap1->out;
	res = ionode->filtres;
};

node filt2 filt::ink2 {
	in = osc1->out;
	cutoff = ionode->lpfcut;
	res = ionode->lpfres;
};

node mixer mixer::mul {
	in0 = filt1->out;
	in1 = env->out;
};

io ionode;