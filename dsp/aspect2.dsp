# $Id$
# Based off the old piano0 dsp
# Leif Ames <ink@bespin.org>
# 5-20-2004

name "Aspect 2";
author "Leif Ames";
description "2 oscs and lots of envelopes";


# min and max of frequency for the band limited osc
	@sfreqlo1 = 1000;
	@sfreqlo1.widget = 1;
	@sfreqlo1.min = 100;
	@sfreqlo1.max = 10000;
	@sfreqhi1 = 9000;
	@sfreqhi1.widget = 1;
	@sfreqhi1.min = 100;
	@sfreqhi1.max = 10000;

# waveform of osc2
	@waveform = 3;
	@waveform.widget = 1;
	@waveform.min = 0;
	@waveform.max = 5.5;

# pulse width
	@pw1 = 0.25;
	@pw1.widget = 1;
	@pw1.min = 0;
	@pw1.max = 1;
	@pw2 = 0.05;
	@pw2.widget = 1;
	@pw2.min = 0;
	@pw2.max = 1;

# osc mixing and pitch offset (for osc2)
	@oscmul1 = 2;
	@oscmul1.widget = 1;
	@oscmul1.min = 0;
	@oscmul1.max = 16;
	@oscmul2 = 1.0002;
	@oscmul2.widget = 1;
	@oscmul2.min = 0;
	@oscmul2.max = 16;
	@oscfade = 0.3;
	@oscfade.widget = 1;
	@oscfade.min = 0;
	@oscfade.max = 1;

# filter minimum and maximum (notes above pitch played)
	@fmin = 24;
	@fmin.widget = 1;
	@fmin.min = 0;
	@fmin.max = 96;
	@fmax = 48;
	@fmax.widget = 1;
	@fmax.min = 0;
	@fmax.max = 96;

# filter res
	@res = 2.0;
	@res.widget = 1;
	@res.min = 0;
	@res.max = 15;

# amp envelope
	@a = 0 ms;
	@a.widget = 1;
	@a.min = 0;
	@a.max = 20000ms;
	@d = 400 ms;
	@d.widget = 1;
	@d.min = 0;
	@d.max = 20000ms;
	@s = 0.3;	# 1 = full, 0 = off
	@s.widget = 1;
	@s.min = 0;
	@s.max = 1;
	@f = 15000 ms;
	@f.widget = 1;
	@f.min = 0;
	@f.max = 20000ms;
	@r = 400 ms;
	@r.widget = 1;
	@r.min = 0;
	@r.max = 20000ms;

# amp envelope2
	@a2 = 100 ms;
	@a2.widget = 1;
	@a2.min = 0;
	@a2.max = 20000ms;
	@d2 = 2000 ms;
	@d2.widget = 1;
	@d2.min = 0;
	@d2.max = 20000ms;
	@s2 = 0.7;	# 1 = full, 0 = off
	@s2.widget = 1;
	@s2.min = 0;
	@s2.max = 1;
	@f2 = 6000 ms;
	@f2.widget = 1;
	@f2.min = 0;
	@f2.max = 20000ms;
	@r2 = 400 ms;
	@r2.widget = 1;
	@r2.min = 0;
	@r2.max = 20000ms;

# overall amp envelope
	@oa = 0 ms;
	@oa.widget = 1;
	@oa.min = 0;
	@oa.max = 20000ms;
	@od = 0 ms;
	@od.widget = 1;
	@od.min = 0;
	@od.max = 20000ms;
	@os = 100%;
	@os.widget = 1;
	@os.min = 0;
	@os.max = 1;
	@of = 20000 ms;
	@of.widget = 1;
	@of.min = 0;
	@of.max = 20000ms;
	@or = 300 ms;
	@or.widget = 1;
	@or.min = 0;
	@or.max = 20000ms;

# filter envelope
	@fa = 1 ms;
	@fa.widget = 1;
	@fa.min = 0;
	@fa.max = 20000ms;
	@fd = 1000 ms;
	@fd.widget = 1;
	@fd.min = 0;
	@fd.max = 20000ms;
	@fs = 90%;
	@fs.widget = 1;
	@fs.min = 0;
	@fs.max = 1;
	@ff = 20000 ms;
	@ff.widget = 1;
	@ff.min = 0;
	@ff.max = 20000ms;
	@fr = 400 ms;
	@fr.widget = 1;
	@fr.min = 0;
	@fr.max = 20000ms;


node ionode {

# standard thinksynth crap

	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = oenv->play;

# min and max of frequency for the band limited osc
#	sfreqlo1 = 1000;
#	sfreqhi1 = 9000;

# waveform of osc2
#	waveform = 3;

# pulse width
#	pw1 = 0.25;
#	pw2 = 0.05;

# osc mixing and pitch offset (for osc2)
#	oscmul1 = 2;
#	oscmul2 = 1.0002;
#	oscfade = 0.3;

# filter minimum and maximum (notes above pitch played)
#	fmin = 24;
#	fmax = 48;

# filter res
#	res = 2.0;

# amp envelope
#	a = 0 ms;
#	d = 400 ms;
#	s = 0.3;	# 1 = full, 0 = off
#	f = 15000 ms;
#	r = 400 ms;

# amp envelope2
#	a2 = 100 ms;
#	d2 = 2000 ms;
#	s2 = 0.7;	# 1 = full, 0 = off
#	f2 = 6000 ms;
#	r2 = 400 ms;

# overall amp envelope
#	oa = 0 ms;
#	od = 0 ms;
#	os = 100%;
#	of = 20000 ms;
#	or = 300 ms;

# filter envelope
#	fa = 1 ms;
#	fd = 1000 ms;
#	fs = 90%;
#	ff = 20000 ms;
#	fr = 400 ms;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node suscalc math::mul {
	in0 = ionode->velocity;
	in1 = @s;
};

node suscalc2 math::mul {
	in0 = ionode->velocity;
	in1 = @s2;
};

node env1 env::adsfr {
	a = @a;
	d = @d;
	s = suscalc->out;
	f = @f;
	r = @r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node env2 env::adsfr {
	a = @a2;
	d = @d2;
	s = suscalc2->out;
	f = @f2;
	r = @r2;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node oenv env::adsfr {
	a = @oa;
	d = @od;
	s = @os;
	f = @of;
	r = @or;
	trigger = ionode->trigger;
};

node fenv env::adsfr {
	a = @fa;
	d = @fd;
	s = @fs;
	f = @ff;
	r = @fr;
	trigger = ionode->trigger;
};

node freqmul1 math::mul {
	in0 = freq->out;
	in1 = @oscmul1;
};

node map1 env::map {  # filter map
	in = fenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = @fmin;
	outmax = @fmax;
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
	outmin = @sfreqlo1;
	outmax = @sfreqhi1;
};

node sqrmap2 env::map {
	in = env2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = @sfreqlo2;
	outmax = @sfreqhi2;
};

node filt filt::res2pole2 {
	in = fademix->out;
	cutoff = filtcalc2->out;
	res = @res;
};

node osc1 osc::softsqr {
	freq = freqmul1->out;
	sfreq = sqrmap1->out;
	pw = @pw1;
};

node osc2 osc::simple {
	freq = freq->out;
	pw = @pw2;
	waveform = @waveform;
	mul = @oscmul2;
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
	fade = @oscfade;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = oenv->out;
};

io ionode;
