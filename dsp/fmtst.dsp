name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	waveform = 4;
	fmmul = 2;
	fmamt = 0.1;
	fmwave = 4;

	fmmul2 = 8;
	fmamt2 = 0.8;
	fmwave2 = 4;

	res = 0.9;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = 20000;
	d = 60000;
	s = 100;
	r = 60000;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = 0;
	outmax = 1;
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
