# $Id: aspect2.dsp,v 1.2 2004/05/25 20:55:12 ink Exp $
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
	sfreqhi1 = 9000;

# waveform of osc2
	waveform = 3;

# pulse width
	pw1 = 0.25;
	pw2 = 0.05;

# osc mixing and pitch offset (for osc2)
	oscmul1 = 2;
	oscmul2 = 1.0002;
	oscfade = 0.3;

# filter minimum and maximum (notes above pitch played)
	fmin = 24;
	fmax = 48;

# filter res
	res = 4.0;

# amp envelope
	a = 0 ms;
	d = 400 ms;
	s = 0.3;	# 1 = full, 0 = off
	f = 15000 ms;
	r = 400 ms;

# amp envelope2
	a2 = 100 ms;
	d2 = 2000 ms;
	s2 = 0.7;	# 1 = full, 0 = off
	f2 = 6000 ms;
	r2 = 400 ms;

# overall amp envelope
	oa = 0 ms;
	od = 0 ms;
	os = 100%;
	of = 20000 ms;
	or = 300 ms;

# filter envelope
	fa = 0 ms;
	fd = 1000 ms;
	fs = 90%;
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

node freqmul1 math::mul {
	in0 = freq->out;
	in1 = ionode->oscmul1;
};

node map1 env::map {  # filter map
	in = fenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fmin;
	outmax = ionode->fmax;
};

node filtcalc1 math::add {
	in0 = ionode->note;
	in1 = map1->out;
};

node filtcalc2 misc::midi2freq {
	note = filtcalc1->out;
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
	cutoff = filtcalc2->out;
	res = ionode->res;
};

node osc1 osc::softsqr {
	freq = freqmul1->out;
	sfreq = sqrmap1->out;
	pw = ionode->pw1;
};

node osc2 osc::simple {
	freq = freq->out;
	pw = ionode->pw2;
	waveform = ionode->waveform;
	mul = ionode->oscmul2;
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
