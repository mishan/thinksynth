name "test";

node ionode test::test {
        out0 = mixer->out;
	out1 = mixer->out;
        channels = 2;
        play = env->play;
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
	r = 40000;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = 256;
	outmin = 0;
	outmax = 1;
};

node map2 env::map {
	in = env->out;
	inmin = -256;
	inmax = 256;
	outmin = 0;
	outmax = 2000;
};

node filt filt::rds {
	in = osc->out;
	cutoff = 0.5;
	res = map1->out;
};

node osc osc::softsqr {
	freq = freq->out;
	sfreq = map2->out;
	pw = 0.3;
};

io ionode;
