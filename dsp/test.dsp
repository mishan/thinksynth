name "test";

node ionode test::test {
	out0 = osc->out;
	channels = 1;
	};
node osc osc::simple {
	freq = 440;
	waveform = testnode->foo;
	};
node testnode test::test {
	foo = 4;
	};

io ionode;
