name "test";

node ionode test::test {
	out0 = mixer->out;
	channels = 1;
	play = env->play;
	};
node mixer mixer::mul {
	in0 = osc->out;
	in1 = env->out;
	};
node env env::adsr {
	a = 100;
	d = 300;
	s = 40;
	r = 500;
	trigger = 1;
	};
node osc osc::simple {
	freq = 440;
	waveform = 4;
	};

io ionode;
