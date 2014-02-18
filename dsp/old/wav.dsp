
name "test";

node ionode {
	out0 = wav->out;
	out1 = wav->out;
	channels = 2;
	play = wav->play;
};

node wav input::wav {
};

io ionode;