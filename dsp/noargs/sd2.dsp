# $Id$
name "test";

node ionode {
	channels = 2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = adsr->play;

	freq = 400;
	freqmul1 = 1.1;
	freqmul2 = 2;
	depth1 = 0.8;
	depth2 = 0.45;
	depth3 = 0.6;

	attack = 20 ms;
	decay = 60 ms;
	midp = 20%;

	cutmin = 0;
	cutmax = 1000;
	res = 0.8;

	waveform = 2;
};

node adsr env::adsr {
	a = 0;
	d = ionode->attack;
	s = ionode->midp;
	r = ionode->decay;
	trigger = 0;
};

node static osc::static {
	stupid_bug = 1;
};

node freqmul1 math::mul {
	in0 = ionode->freq;
	in1 = ionode->freqmul1;
};

node freqmul2 math::mul {
	in0 = ionode->freq;
	in1 = ionode->freqmul2;
};

node osc1 osc::simple {
	freq = ionode->freq;
	waveform = ionode->waveform;
	fm = static->out;
	fmamt = ionode->depth1;
};

node osc2 osc::simple {
	freq = freqmul1->out;
	waveform = ionode->waveform;
	fm = static->out;
	fmamt = ionode->depth2;
};

node osc3 osc::simple {
	freq = freqmul2->out;
	waveform = ionode->waveform;
	fm = static->out;
	fmamt = ionode->depth3;
};

node ringmod1 mixer::mul {
	in0 = osc1->out;
	in1 = osc2->out;
};

node ringmod2 mixer::mul {
	in0 = ringmod1->out;
	in1 = osc3->out;
};

node filtmap env::map {
	in = adsr->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
};

node filter filt::res2pole {
	in = ringmod2->out;
	cutoff = filtmap->out;
	res = ionode->res;
};

node mixer mixer::mul {
	in0 = filter->out_high;
	in1 = adsr->out;
};

io ionode;
