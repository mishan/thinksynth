# $Id: wav.dsp,v 1.1 2004/09/15 06:07:43 ink Exp $

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