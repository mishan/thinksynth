# Piano-like synth
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	sfreqlo = 1600;
	sfreqhi = 5500;

	pw1 = 0.38;
	pw2 = 0.75;

	res = 0.5;

	oscmul = 1.0001;
	oscfade = 0.25;

	a = 0.7 ms;
	d = 50 ms;
	s = 0.420;	# 1 = full, 0 = off
	f = 7000 ms;
	r = 400 ms;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node suscalc math::mul {
	in0 = ionode->velocity;
	in1 = ionode->s;
};

node env env::adsfr {
	a = ionode->a;
	d = ionode->d;
	s = suscalc->out;
	f = ionode->f;
	r = ionode->r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node freqmul math::mul {
	in0 = freq->out;
	in1 = ionode->oscmul;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 0;
	outmax = 0.98;
};

node map2 env::map {
	in = env->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->sfreqlo;
	outmax = ionode->sfreqhi;
};

node filt filt::ink {
	in = mixer2->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc osc::softsqr {
	freq = freq->out;
	sfreq = map2->out;
	pw = ionode->pw1;
};

node osc2 osc::softsqr {
	freq = freqmul->out;
	sfreq = map2->out;
	pw = ionode->pw2;
};

node mixer2 mixer::fade {
	in0 = osc->out;
	in1 = osc2->out;
	fade = ionode->oscfade;
};

io ionode;
