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
	in0 = osc->out;
	in1 = env->out;
	};
node env env::adsr {
	a = 1000;
	d = 3000;
	s = 40;
	r = 20000;
	trigger = 0;
	};
node osc osc::simple {
	freq = freq->out;
	waveform = 4;
	};

io ionode;
