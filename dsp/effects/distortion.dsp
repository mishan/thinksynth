name "test";

node ionode {
	out0 = delay->out;
	out1 = delay2->out;
	channels = 2;
	play = wav->play;

	flfo = 0.0314;
	cutmin = 0.3;
	cutmax = 0.9;
	res = 0.2;
	fwave = 5;

	dlfo = 0.05;
	dlen = 44100;
	delaymin = 1000;
	delaymax = 1700;

	dlfo2 = 0.0502;
	delaymin2 = 1700;
	delaymax2 = 1000;

	slfo = 0.151;
	satmin = 1;
	satmax = 2;

	dfeed = 0.25;
	ddry = 0.25;
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
	in = filt->out;
	size = ionode->dlen;
	delay = delaymap->out;
	feedback = ionode->dfeed;
	dry = ionode->ddry;
};

node dlfo2 osc::simple {
	freq = ionode->dlfo2;
};

node delaymap2 env::map {
	in = dlfo2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->delaymin2;
	outmax = ionode->delaymax2;
};

node delay2 delay::echo {
	in = filt->out;
	size = ionode->dlen;
	delay = delaymap2->out;
	feedback = ionode->dfeed;
	dry = ionode->ddry;
};

node filt filt::res1pole {
	in = sat->out;
	cutoff = filtermap->out;
	res = ionode->res;
};

node sat dist::inksat {
	in = wav->out;
	factor = satmap->out;
};

io ionode;