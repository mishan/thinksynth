# $Id: fmtst.dsp,v 1.11 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;


	filtmax = 0.98;
	filtmin = 0.01;

	waveform = 5;
	fmmul1 = 2;
	fmamt = 3;
	fmwave = 5;

	fmmul2 = 0.25;
	fmamt2 = 1;
	fmwave2 = 5;

	res = 0.5;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::ad {
	a = 1000;
	d = 19000;
	trigger = 0;
};

node fenv env::ad {
	a = 6000;
	d = 14000;
	trigger = 0;
};

node map1 env::map {
	in = fenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->filtmin;
	outmax = ionode->filtmax;
};

node filt filt::res1pole {
	in = osc3->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->fmwave;
	mul = ionode->fmmul1;
};

node osc2 osc::simple {
	freq = freq->out;
	waveform = ionode->fmwave2;
	fm = osc1->out;
	fmamt = ionode->fmamt2;
	mul = ionode->fmmul2;
};

node osc3 osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	fm = osc2->out;
	fmamt = ionode->fmamt;
};

io ionode;
