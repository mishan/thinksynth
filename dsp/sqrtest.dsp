name "test";

node ionode test::test {
    out0 = mixer->out;
	out1 = mixer->out;
    channels = 2;
    play = env->play;
	test = freq->foo;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = 2000;
	d = 4000;
	s = 100;
	r = 10000;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = 256;
	outmin = 0;
	outmax = 1;
};

node filt filt::rds {
	in = mixer2->out;
	cutoff = 0.8;
	res = map1->out;
};

node mixer2 osc::softsqr {
	freq = freq->out;
	sw = 0.1;
	pw = 0.1;
};

io ionode;
