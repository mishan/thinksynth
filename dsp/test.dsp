name "test";

node ionode test::test {
	out0 = env->out;
	channels = 1;
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
	waveform = testnode->foo;
	};
node testnode test::test {
	foo = 4;
	};

io ionode;
