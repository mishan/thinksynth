# $Id: smack0.dsp,v 1.2 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	channels = 2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = adsr->play;

	freq = 900;
	freqmul1 = 3.14;
	freqmul2 = 4.62;
	depth1 = 0.1;
	depth2 = 0.25;
	depth3 = 0.3;

	attack = 2000;
	decay = 4000;
	midp = 30;

	cutmin = 1700;
	cutmax = 400;
	res = 2;

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