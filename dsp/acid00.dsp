# Written by Leif Ames <ink@bespin.org>
# 5-24-2003

name "test";

node ionode {
	channels = 2;
	out0 = notemix->out;
	out1 = notemix->out;
	play = 1;

	env_a = 2000;
	env_d = 100000;
	env_s = 100;
	env_r = 10000;

	filt_a = 10000;
	filt_s = 30;
	cutlo = 500;
	cuthi = 5000;
	res = 0.8;

	wave = 1;
	pw = 0.7;

	subwave = 5;
	subpw = 0.4;

	supermod = 4;
	superwave = 1;
	superpw = 0.2;

	mainfade = 0.4;
	subsuperfade = 0.4;

	trigger = 0;
};

node env env::adsr {
	a = ionode->env_a;
	d = ionode->env_d;
	s = ionode->env_s;
	r = ionode->env_r;
	trigger = ionode->trigger;
};

node notemix mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node filt_a_add math::add {
	in0 = ionode->env_a;
	in1 = ionode->filt_a;
};

node filtenv env::adsr {
	a = filt_a_add->out;
	d = ionode->env_d;
	s = ionode->filt_s;
	r = ionode->env_r;
	trigger = ionode->trigger;
};

node filtmap env::map {
	in = filtenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutlo;
	outmax = ionode->cuthi;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node superfreq math::mul {
	in0 = freq->out;
	in1 = ionode->mod;
};

node subfreq math::div {
	in0 = freq->out;
	in1 = 2;
};

node subosc osc::simple {
	freq = subfreq->out;
	waveform = ionode->subwave;
	pw = ionode->subpw;
};

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
	pw = ionode->pw;
#	reset = subosc->sync;  # the freq input is slightly off, must fix drifts
};

node superosc osc::simple {
	freq = superfreq->out;
	waveform = ionode->superwave;
	reset = osc->sync;
};

node superenvosc osc::simple {
	freq = freq->out;
	waveform = 1;
	pw = ionode->superpw;
	reset = subosc->sync;
};

node superenvmap env::map {
	in = superenvosc->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = 0;
};

node supermixer math::mul {
	in0 = superosc->out;
	in1 = superenvmap->out;
};

node subsupermixer mixer::fade {
	in0 = subosc->out;
	in1 = supermixer->out;
	fade = ionode->subsuperfade;
};

node oscmixer mixer::fade {
	in0 = osc->out;
	in1 = subsupermixer->out;
	fade = ionode->mainfade;
};

node filt filt::res2pole {
	in = oscmixer->out;
	cutoff = filtmap->out;
	res = ionode->res;
};

io ionode;
