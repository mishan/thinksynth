name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = wav->play;

	falloff = 3.4;

	cutmin = 0.07;
	cutmax = 1.4;
	resmax = 1.4;
	resmin = 0.1;

	sat = 4;

	gatecut = 4;
	gateroll = 1;
};

node wav input::wav {
};

node follower env::followavg {
	in = wav->out;
	falloff = ionode->falloff;
};

node filtermap env::map {
	in = follower->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
};

node resmap env::map {
	in = follower->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->resmin;
	outmax = ionode->resmax;
};

node gate misc::noisegate {
	in = wav->out;
	rolloff = ionode->gateroll;
	cutoff = ionode->gatecut;
};

node sat dist::inksat {
	in = gate->out;
	factor = ionode->sat;
};

node filt filt::ink2 {
	in = sat->out;
	cutoff = filtermap->out;
	res = resmap->out;
};

io ionode;