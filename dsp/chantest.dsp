# $Id: chantest.dsp,v 1.2 2004/08/01 09:36:27 ink Exp $
name "test";

@cutoff = 0.5;
@res = 0.4;
@cutoff.widget = 1;
@res.widget = 1;

node ionode {
	channels = 2;
	out0 = lmixer->out;
	out1 = rmixer->out;
	play = env->play;

	centerdetune = -0.523;
	ldetune = 0.94;
	rdetune = 1.121;

	centermix = 0.3;
	sidemix = 0.3;

	centerwave1 = 1;
	centerwave2 = 5;
	lwave = 1;
	rwave = 1;

	vmin = 0.06; # .02
	vmax = 0.9;

	cutoff = vmap->out;
	res = 0.8;

	amp = 0.6;			# Amplitude before filtering

	a = 800;
	d = 2000;
	s = 0.7;	# 1 = full, 0 = off
	r = 6000;
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

node vmap env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->vmin;
	outmax = ionode->vmax;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node centerdetune math::add {
	in0 = freq->out;
	in1 = ionode->centerdetune;
};

node ldetune math::add {
	in0 = freq->out;
	in1 = ionode->ldetune;
};

node rdetune math::add {
	in0 = freq->out;
	in1 = ionode->rdetune;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->centerwave1;
};

node osc2 osc::simple {
	freq = centerdetune->out;
	waveform = ionode->centerwave2;
};

node osc3 osc::simple {
	freq = ldetune->out;
	waveform = ionode->lwave;
};

node osc4 osc::simple {
	freq = rdetune->out;
	waveform = ionode->rwave;
};

node centermix mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->centermix;
};

node lmix mixer::fade {
	in0 = osc3->out;
	in1 = centermix->out;
	fade = ionode->sidemix;
};

node rmix mixer::fade {
	in0 = osc4->out;
	in1 = centermix->out;
	fade = ionode->sidemix;
};

node lamp math::mul {
	in0 = lmix->out;
	in1 = ionode->amp;
};

node ramp math::mul {
	in0 = rmix->out;
	in1 = ionode->amp;
};

node lfilt filt::ink2 {
	in = lamp->out;
	cutoff = @cutoff;
	res = @res;
};

node rfilt filt::ink2 {
	in = ramp->out;
	cutoff = @cutoff;
	res = @res;
};

node lmixer mixer::mul {
	in0 = lfilt->out;
	in1 = env->out;
};

node rmixer mixer::mul {
	in0 = rfilt->out;
	in1 = env->out;
};

io ionode;