# $Id$
name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = 1;

	cutlfo = 0.1;
	reslfo = 0.272;
	freqlfo = 0.7;

	mincut = 0.1;
	maxcut = 1;
	minres = 0.2;
	maxres = 0.9;
	minfreq = 25; 
	maxfreq = 100;

	sqrsfreq = 8000;
	sqrpw = 0.05;

};

node cutlfo osc::simple {
	freq = ionode->cutlfo;
	waveform = 3;
};

node reslfo osc::simple {
	freq = ionode->reslfo;
	waveform = 3;
};

node freqlfo osc::simple {
	freq = ionode->freqlfo;
	waveform = 0;
};

node cutmap env::map {
	in = cutlfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->mincut;
	outmax = ionode->maxcut;
};

node resmap env::map {
	in = reslfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->minres;
	outmax = ionode->maxres;
};

node freqmap env::map {
	in = freqlfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->minfreq;
	outmax = ionode->maxfreq;
};

node filt filt::ink {
	in = osc->out;
	cutoff = cutmap->out;
	res = resmap->out;
};

node print misc::print {
	in = freqmap->out;
};

node osc osc::simple{
	freq = freqmap->out;
	waveform = 1;
	pw = ionode->sqrpw;
};

io ionode;
