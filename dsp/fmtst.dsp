name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;


	filtmax = 0.9;
	filtmin = 0.01;

	waveform = 0;
	fmmul1 = 1;
	fmamt = 0.2;
	fmwave = 2;

	fmmul2 = 1;
	fmamt2 = 0.3;
	fmwave2 = 1;

	res = 0.7;
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
	d = 150000;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->filtmin;
	outmax = ionode->filtmax;
};

node filt filt::ink {
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
