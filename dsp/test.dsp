name "test";

node ionode test::test {
	out0 = osc->out;
	channels = 1;
	};
node osc osc::simple {
	freq = 440;
	waveform = 4;
	};

io ionode;
