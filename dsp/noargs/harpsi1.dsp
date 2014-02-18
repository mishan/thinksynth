# Piano-like synth
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	sfreqlo = 0;
	sfreqhi = 4000;

	filtmin = 0.1;
	filtmax = 1;
	resmin = 0.5;
	resmax = 1;

	osc2detune = 0.1;
	mix2fade = 0.35;

	a = 0;
	d = 2000;
	s = 0.5;	# 1 = full, 0 = off
	r = 8000;
};

node freq misc::midi2freq {
	note = ionode->note;
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

node detune math::add {
	in0 = freq->out;
	in1 = ionode->osc2detune;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->filtmin;
	outmax = ionode->filtmax;
};

node map2 env::map {
	in = env->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->sfreqlo;
	outmax = ionode->sfreqhi;
};

node map3 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->resmin;
	outmax = ionode->resmax;
};

node filt filt::ink2 {
	in = mixer2->out;
	cutoff = map1->out;
	res = map3->out;
};

node osc osc::softsqr {
	freq = freq->out;
	sfreq = map2->out;
	pw = 0.85;
};

node osc2 osc::softsqr {
	freq = detune->out;
	sfreq = map2->out;
	pw = 0.15;
};

node mixer2 mixer::fade {
	in0 = osc->out;
	in1 = osc2->out;
	fade = ionode->mix2fade;
};

io ionode;
