name "test";

node ionode {
	out0 = sat->out;
	out1 = sat->out;
	channels = 2;
	play = wav->play;

	flfo = 11;
	cutmin = 0.3;
	cutmax = 0.8;
	res = 0.5;
	fwave = 3;

	dlfo = 0.0232;
	dlen = 44100;
	delaymin = 1000;
	delaymax = 1300;

	slfo = 0.1;
	satmin = 1;
	satmax = 3;

	dfeed = 0.2;
	ddry = 0.75;
};

node wav input::wav {
};

node slfo osc::simple {
	freq = ionode->flfo;
	waveform = ionode->fwave;
};

node satmap env::map {
	in = slfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->satmin;
	outmax = ionode->satmax;
};

node dlfo osc::simple {
	freq = ionode->dlfo;
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
	in = dlfo->out;
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

node filt filt::ink {
	in = sat->out;
	cutoff = filtermap->out;
	res = ionode->res;
};

node sat dist::inksat {
	in = delay->out;
	factor = satmap->out;
};

io ionode;