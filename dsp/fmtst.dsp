name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;


	filtmax = 0.6;
	filtmin = 0.3;

	waveform = 3;
	fmmul = 2;
	fmamt = 0.5;
	fmwave = 5;

	fmmul2 = 1;
	fmamt2 = 0.2;
	fmwave2 = 5;

	res = 0.4;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = 0;
	d = 5000;
	s = 90;
	r = 10000;
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

node fmfreq math::mul {
	in0 = freq->out;
	in1 = ionode->fmmul;
};

node fmfreq2 math::mul {
	in0 = freq->out;
	in1 = ionode->fmmul2;
};

node osc1 osc::simple {
	freq = fmfreq->out;
	waveform = ionode->fmwave;
};

node osc2 osc::simple {
	freq = fmfreq2->out;
	waveform = ionode->fmwave2;
	fm = osc1->out;
	fmamt = ionode->fmamt2;
};

node osc3 osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	fm = osc2->out;
	fmamt = ionode->fmamt;
};

io ionode;
