# $Id$
name "test";

node ionode {
	channels = 2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = env->play;

	dmax = 48000;	# this should be way bigger than we will ever need
	dlen = 1.05;

	dfeed1 = 0.2;
	ddry1 = 0.0;
	dfade1 = 0.2;

	a = 10ms;
	d = 200ms;
	s = 0.5;
	r = 500ms;

	wave = 3;

	cutmul = 2;
	res = 3;

	distgain = 3;
};

node freq misc::midi2freq {
	note = ionode->note;
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

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
};

node wavelength misc::freq2samples {
	freq = freq->out;
};

node wavecalc1 math::mul {
	in0 = wavelength->out;
	in1 = ionode->dlen;
};

node cutmul math::mul {
	in0 = freq->out;
	in1 = ionode->cutmul;
};

node dfilt1 filt::res2pole2 {
	in = osc->out;
	cutoff = cutmul->out;
	res = ionode->res;
};

node ddist1 dist::saturate {
	in = delay1->out;
	factor = ionode->distgain;
};

node premix mixer::fade {
	in0 = dfilt1->out;
	in1 = ddist1->out;
	fade = ionode->dfade1;
};

node delay1 delay::echo {
	in = premix->out;
	size = ionode->dmax;
	delay = wavecalc1->out;
	feedback = ionode->dfeed1;
	dry = ionode->ddry1;
};

node mixer mixer::mul {
	in0 = premix->out;
	in1 = env->out;
};

io ionode;