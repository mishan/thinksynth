name "test";

node ionode test::test {
    out0 = mixer->out;
	out1 = mixer->out;
    channels = 2;
    play = env->play;
    };
node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
	};
node env env::adsr {
	a = 0;
	d = 5000;
	s = 70;
	r = 2000;
	trigger = 0;
	};
node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 25;
	outmax = 100;
	};
node map2 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = 0;
    outmax = 1;
    };
node filt filt::rds {
	in = osc->out;
	cutoff = 0.4;
	res = map2->out;
	};
node osc osc::simple {
	freq = map1->out;
	waveform = 4;
	};

io ionode;
