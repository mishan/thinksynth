name "test";

node ionode test::test {
        out0 = filt->out;
	out1 = filt->out;
        channels = 2;
        play = env->play;
	test = freq->foo;
        };
node freq misc::midi2freq {
	note = ionode->note;
	};
node mixer mixer::mul {
	in0 = osc->out;
	in1 = env->out;
	};
node env env::adsr {
	a = 3000;
	d = 4000;
	s = 40;
	r = 5000;
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
	in = osc->out;
	cutoff = map1->out;
	res = 0.8;
	};
node osc osc::simple {
	freq = freq->out;
	waveform = 4;
	};

io ionode;
