name "test";

node ionode test::test {
        out0 = mixer->out;
        channels = 1;
        play = env->play;
        };
node freq misc::midi2freq {
	note = ionode->note;
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
	freq = freq->out;
	waveform = 4;
	};

io ionode;
