name "test";

node ionode {
    out0 = mixer->out;
	out1 = mixer->out;
    channels = 2;
    play = env->play;

	velcalcmin = 0;
	velcalcmax = 3000;
#	blah = print->foo;
};

node print misc::print {
	in = freq->out;
};

node velcalc env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->velcalcmin;
	outmax = ionode->velcalcmax;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = 0;
	d = 8000;
	s = 100;
	r = 4000;
	trigger = ionode->trigger;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 0;
	outmax = 1;
};

node map2 env::map {
	in = env->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 0;
	outmax = velcalc->out;
};

node filt filt::ink2 {
	in = osc->out;
	cutoff = 0.2;
	res = map1->out;
};

node osc osc::softsqr {
	freq = freq->out;
	sfreq = map2->out;
	pw = 0.3;
};

io ionode;
