name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	filtlo = 0.4;
	filthi = 0.95;
	res = 0.8;

	bandlow = 50;
	bandhi = 2400;

	pw1 = 0.1;
	pw2 = 0.2;
	pw3 = 0.3;
	freq2mul = 1.01;
	freq3mul = 1.99;

	mix1 = 0.4;	# osc 1 and osc2
	mix2 = 0.2;	# mix1 and osc3

	a = 0;
	d = 3000;
	s = 0.4;
	r = 10000;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node freq2 math::mul {
	in0 = freq->out;
	in1 = ionode->freq2mul;
};

node freq3 math::mul {
        in0 = freq->out;
	in1 = ionode->freq3mul;
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
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node map2 env::map {
	in = env->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->bandlo;
	outmax = ionode->bandhi;
};

node filt filt::ink2 {
	in = mix2->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc1 osc::softsqr {
	freq = freq->out;
	sfreq = map2->out;
	pw = ionode->pw1;
};

node osc2 osc::bandosc {
        freq = freq2->out;
        band = map2->out;
        pw = ionode->pw2;
};

node osc3 osc::bandosc {
        freq = freq3->out;
        band = map2->out;
        pw = ionode->pw3;
};

node mix1 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->mix1;
};

node mix2 mixer::fade {
        in0 = mix1->out;
        in1 = osc3->out;
        fade = ionode->mix2;
};

io ionode;
