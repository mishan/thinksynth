name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	waveform = 4;

	fmmul = 4;
	fmamt = 0.1;
	fmwave = 0;

	pwmul = 0.25;
	pwwave = 0;

	res = 0.8;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::adsr {
	a = 1000;
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

node pwfreq math::mul {
	in0 = freq->out;
	in1 = ionode->pwmul;
};

node osc1 osc::simple {
	freq = fmfreq->out;
	waveform = ionode->fmwave;
};

node osc2 osc::simple {
	freq = pwfreq->out;
	waveform = ionode->pwwave;
};

node pwmap env::map {
	in = osc2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 0.1;
	outmax = 0.5;
};

node osc3 osc::simple {
	freq = freq->out;
	waveform = 2;
	fm = osc1->out;
	fmamt = ionode->fmamt;
	pw = pwmap->out;
};

io ionode;
