# $Id: wav.dsp,v 1.4 2004/02/09 10:50:28 misha Exp $

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