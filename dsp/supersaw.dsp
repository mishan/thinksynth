name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
    channels = 2;
    play = env->play;

	res = 0.2;

	a = 8000;
	d = 4000;
	s = 0.75;
	r = 10000;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
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

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 0.05;
	outmax = 1;
};

node filt filt::ink2 {
	in = osc->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc osc::multisined {
	freq = freq->out;
	waveform = 2;

	waves = 8;

	ampmul = 0.98;
	ampadd = 0;

	detunefreq = 1.714;
	detuneamt = 0.015;
};

io ionode;
