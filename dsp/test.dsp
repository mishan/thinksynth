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
	a = 1000;
	d = 6000;
	s = 40;
	r = 3000;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = 256;
	outmin = 1;
	outmax = 0;
};

node filt filt::rds {
	in = mixer2->out;
	cutoff = map1->out;
	res = 0.9;
};

node osc osc::simple {
	freq = freq->out;
	waveform = 2;
};

node osc2 osc::simple {
	freq = freq->out;
	waveform = 4;
};

node mixer2 mixer::add {
	in0 = osc->out;
	in1 = osc2->out;
};

io ionode;
