name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = wav->play;

	flfo = 0.2;
	cutmin = 300;
	cutmax = 2000;
	res = 3;
	fwave = 0;

	dlfo = 0.1;
	dlen = 20000;
	dwave = 3;
	delaymin = 000;
	delaymax = 2000; # must correlate this to input frequency!  gargh!

	dfeed = 0;
	ddry = 0.6;
};

node wav input::wav {
};

node dlfo osc::simple {
	freq = ionode->dlfo;
	waveform = ionode->dwave;
};

node flfo osc::simple {
	freq = ionode->flfo;
	waveform = ionode->fwave;
};

node filtermap env::map {
	in = flfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
};

node dlfo osc::simple {
	freq = ionode->dlfo;
};

node delaymap env::map {
	in = wav->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->delaymin;
	outmax = ionode->delaymax;
};

node delay delay::echo {
	in = wav->out;
	size = ionode->dlen;
	delay = delaymap->out;
	feedback = ionode->dfeed;
	dry = ionode->ddry;
};

node filt filt::res2pole {
	in = delay->out;
	cutoff = filtermap->out;
	res = ionode->res;
};

io ionode;