# Synth Organ
# Leif Ames <ink@bespin.org>
# 4/7/2004

name "test";

node ionode {
	channels = 2;
	out0 = envmixer->out;
	out1 = envmixer->out;
	play = env->play;

	vmin = 0.01;
	vmax = 1;

	fade = vmap->out;

	band1 = 2;
	band2 = 4;
	band3 = 6;
	band4 = 8;

	a = 5ms;
	d = 20ms;
	s = 0.8;
	r = 300ms;

	fa = 10ms;
	fd = 100ms;
	fs = 0.6;
	fr = 500ms;

	cutmin = 0.3;
	cutmax = 0.1;
	res = 0.3;
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

node bandcalc1 math::mul {
	in0 = freq->out;
	in1 = ionode->band1;
};

node bandcalc2 math::mul {
	in0 = freq->out;
	in1 = ionode->band2;
};

node bandcalc3 math::mul {
	in0 = freq->out;
	in1 = ionode->band3;
};

node bandcalc4 math::mul {
	in0 = freq->out;
	in1 = ionode->band4;
};

node osc1 osc::softsqr {
	freq = freq->out;
	sfreq = bandcalc1->out;
};

node osc2 osc::softsqr {
    freq = freq->out;
	sfreq = bandcalc2->out;
};

node osc3 osc::softsqr {
    freq = freq->out;
	sfreq = bandcalc3->out;
};

node osc4 osc::softsqr {
    freq = freq->out;
	sfreq = bandcalc4->out;

};

node submix1 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->fade;
};

node submix2 mixer::fade {
	in0 = osc3->out;
	in1 = osc4->out;
	fade = ionode->fade;
};

node mixer mixer::fade {
	in0 = submix1->out;
	in1 = submix2->out;
	fade = ionode->fade;
};

node filtsuscalc math::mul {
	in0 = ionode->velocity;
	in1 = ionode->fs;
};

node filtenv env::adsr {
	a = ionode->fa;
	d = ionode->fd;
	s = filtsuscalc->out;
	r = ionode->fr;
	trigger = ionode->trigger;
};

node filtmap env::map {
	in = filtenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
};

node filt filt::moog {
	in = mixer->out;
	cutoff = filtmap->out;
	res = ionode->res;
};

node envmixer mixer::mul {
	in0 = filt->out_low;
	in1 = env->out;
};

io ionode;
