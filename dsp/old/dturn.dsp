# $Id$
name "test";

node ionode {
	channels = 2;
	out0 = delay->out;
	out1 = delay->out;
	play = 1;

	dlen = 100;
	dfreq = freq->out;
	dwave = 0;
	dfeed = 0;
	ddry = 0;

	waveform = 1;
	cutoff = 0.7;
	res = 0.4;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
};

node dosc osc::simple {
	freq = ionode->dfreq;
	waveform = ionode->dwave;
};

node dmap env::map {
	in = dosc->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = ionode->dlen;
};

node filt filt::ink {
	in = osc->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
};

node delay delay::echo {
	in = filt->out;
	size = ionode->dlen;
	delay = dmap->out;
	feedback = ionode->dfeed;
	dry = ionode->ddry;
};

io ionode;