name "test";

node ionode test::test {
        out0 = osc->out;
	out1 = osc->out;
        channels = 2;
        play = env->play;
        };
#node freq misc::midi2freq {
	#note = ionode->note;
	#};
node mixer mixer::mul {
	in0 = osc->out;
	in1 = env->out;
	};
node env env::adsr {
	a = 100;
	d = 6000;
	s = 40;
	r = 500;
	trigger = 0;
	};
node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = 256;
	outmin = 25;
	outmax = 120;
	};
#node filt filt::rds {
	#in = osc->out;
	#cutoff = map1->out;
	#res = 0.6;
	#};
node osc osc::simple {
	freq = map1->out;
	waveform = 4;
	};

io ionode;
