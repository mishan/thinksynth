name "test";

node ionode test::test {
	out = osc->out;
	};
node osc osc::simple {
	freq = 440;
	waveform = 0;
	};

io ionode;
