name "test";

node ionode {
	out0 = wav->out;
	out1 = wav->out;
	channels = 2;
	play = 1;
};

node wav input::wav {
};

io ionode;