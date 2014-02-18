name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
    channels = 2;
    play = env->play;

	fmax = 150;
	fmin = 0;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = osc->out;
	in1 = env->out;
};

node env env::adsr {
	a = 0;
	d = 3000;
	s = 80;
	r = 3000;
	trigger = 0;
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
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fmin;
	outmax = ionode->fmax;
};

node osc osc::multiwave {
	freq = map2->out;
	amp = env->out;
	waves = 10;
	pitchmul = 1.045;
	pitchadd = 0.5;
	ampmul = 0.95;
	ampadd = 0;
};

io ionode;
