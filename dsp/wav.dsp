name "test";

node ionode {
	out0 = mixer->out;
	channels = 1;
	play = 1;
};

node mixer mixer::mul {
	in0 = wav->out;
};

node wav input::wav {
};

io ionode;