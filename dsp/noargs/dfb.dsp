name "test";
# delay feedback
# leif ames 1-2-2004

node ionode {
	channels = 2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = env->play;

	dlen = 44100;
	fmamt = 0.1;
	fmul = 1;
	pmul = 2;
	dfreq = freqmul->out;
	dwave = 3;
	dfeed = 0.3;
	ddry = 0;

	waveform = 1;
	cutoff = 0.24;
	res = 0.7;

	a = 2800;
	d = 4000;
	s = 0.7;	# 1 = full, 0 = off
	r = 5000;
};

node suscalc math::mul {
	in0 = ionode->velocity;
	in1 = ionode->s;
};

node env env::adsr {
	a = ionode->a;
	d = ionode->d;
	s = suscalc->out;
	r = ionode->r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node freqmul math::mul {
	in0 = freq->out;
	in1 = ionode->fmul;
};

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	fm = delay->out;
	fmamt = ionode->fmamt;
};

node dosc osc::simple {
	freq = freqmul->out;
	waveform = ionode->dwave;
};

node dcalc misc::freq2samples {
	in = freq->out;
};

node dmul math::mul {
	in0 = freq->out;
	in1 = ionode->pmul;
};

node dmap env::map {
	in = dosc->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = dmul->out;
};

node filt filt::ink2 {
	in = osc->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
};

node delay delay::echo {
	in = osc->out;
	size = ionode->dlen;
	delay = dmap->out;
	feedback = ionode->dfeed;
	dry = ionode->ddry;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

io ionode;