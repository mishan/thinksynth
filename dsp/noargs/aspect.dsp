# $Id$
# Based off the old piano0 dsp
# Leif Ames <ink@bespin.org>
# 5-20-2004

name "test";

node ionode {

# standard thinksynth crap

	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = oenv->play;

# min and max of frequency for the band limited osc
	sfreqlo1 = 1000;
	sfreqhi1 = 4000;
	sfreqlo2 = 3000;
	sfreqhi2 = 9000;

# pulse width
	pw1 = 0.45;
	pw2 = 0.22;

# osc mixing and pitch offset (for osc2)
	oscmul = 2.0004;
	oscfade = 0.35;

# filter minimum and maximum
	fmin = 200;
	fmax = 5000;

# filter res
	res = 2.0;

# amp envelope
	a = 20 ms;
	d = 400 ms;
	s = 0.3;	# 1 = full, 0 = off
	f = 15000 ms;
	r = 400 ms;

# amp envelope2
	a2 = 2 ms;
	d2 = 200 ms;
	s2 = 0.5;	# 1 = full, 0 = off
	f2 = 6000 ms;
	r2 = 400 ms;

# overall amp envelope
	oa = 0 ms;
	od = 0 ms;
	os = 100%;
	of = 20000 ms;
	or = 400 ms;

# filter envelope
	fa = 10 ms;
	fd = 1000 ms;
	fs = 30%;
	ff = 20000 ms;
	fr = 400 ms;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node suscalc math::mul {
	in0 = ionode->velocity;
	in1 = ionode->s;
};

node suscalc2 math::mul {
	in0 = ionode->velocity;
	in1 = ionode->s2;
};

node env1 env::adsfr {
	a = ionode->a;
	d = ionode->d;
	s = suscalc->out;
	f = ionode->f;
	r = ionode->r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node env2 env::adsfr {
	a = ionode->a2;
	d = ionode->d2;
	s = suscalc2->out;
	f = ionode->f2;
	r = ionode->r2;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node oenv env::adsfr {
	a = ionode->oa;
	d = ionode->od;
	s = ionode->os;
	f = ionode->of;
	r = ionode->or;
	trigger = ionode->trigger;
};

node fenv env::adsfr {
	a = ionode->fa;
	d = ionode->fd;
	s = ionode->fs;
	f = ionode->ff;
	r = ionode->fr;
	trigger = ionode->trigger;
};

node freqmul math::mul {
	in0 = freq->out;
	in1 = ionode->oscmul;
};

node map1 env::map {  # filter map
	in = fenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fmin;
	outmax = ionode->fmax;
};

node sqrmap1 env::map {
	in = env1->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->sfreqlo1;
	outmax = ionode->sfreqhi1;
};

node sqrmap2 env::map {
	in = env2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->sfreqlo2;
	outmax = ionode->sfreqhi2;
};

node filt filt::res2pole2 {
	in = fademix->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc1 osc::softsqr {
	freq = freq->out;
	sfreq = sqrmap1->out;
	pw = ionode->pw1;
};

node osc2 osc::softsqr {
	freq = freqmul->out;
	sfreq = sqrmap2->out;
	pw = ionode->pw2;
};

node mixer1 mixer::mul {
	in0 = osc1->out;
	in1 = env1->out;
};

node mixer2 mixer::mul {
	in0 = osc2->out;
	in1 = env2->out;
};

node fademix mixer::fade {
	in0 = mixer1->out;
	in1 = mixer2->out;
	fade = ionode->oscfade;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = oenv->out;
};

io ionode;
