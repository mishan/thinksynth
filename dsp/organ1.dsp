# $Id: organ1.dsp,v 1.3 2004/04/17 23:59:04 ink Exp $
# Synth Organ
# Leif Ames <ink@bespin.org>
# 4/7/2004

name "test";

node ionode {
	channels = 2;
	out0 = envmixer->out;
	out1 = envmixer->out;
	play = env->play;

	vmin = 0.01;
	vmax = 1;

	fade = vmap->out;
	waveform = 5;

	band1 = 2;
	band2 = 4;
	band3 = 8;
	band4 = 16;

	a = 100;
	d = 1000;
	s = 0.5;
	r = 6000;

	fa = 0;
	fd = 3000;
	fs = 0.8;
	fr = 6000;

	cutmin = 0;
	cutmax = 0.2;
	res = 0.25;
};

node vel misc::midi2range {
	in = ionode->velocity;
};

node suscalc math::mul {
	in0 = vel->out;
	in1 = ionode->s;
};

node env env::adsr {
	a = ionode->a;
	d = ionode->d;
	s = suscalc->out;
	r = ionode->r;
	p = vel->out;
	trigger = ionode->trigger;
};

node vmap env::map {
	in = vel->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->vmin;
	outmax = ionode->vmax;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node bandcalc1 math::mul {
	in0 = freq->out;
	in1 = ionode->band1;
};

node bandcalc2 math::mul {
	in0 = freq->out;
	in1 = ionode->band2;
};

node bandcalc3 math::mul {
	in0 = freq->out;
	in1 = ionode->band3;
};

node bandcalc4 math::mul {
	in0 = freq->out;
	in1 = ionode->band4;
};

node osc1 osc::softsqr {
	freq = freq->out;
	sfreq = bandcalc1->out;
	waveform = ionode->waveform;
};

node osc2 osc::softsqr {
        freq = freq->out;
	sfreq = bandcalc2->out;
        waveform = ionode->waveform;
	mul = 2;
#	reset = osc1->sync;
};

node osc3 osc::softsqr {
        freq = freq->out;
		sfreq = bandcalc3->out;
        waveform = ionode->waveform;
        mul = 4;
#        reset = osc2->sync;
};

node osc4 osc::softsqr {
        freq = freq->out;
		sfreq = bandcalc4->out;
        waveform = ionode->waveform;
        mul = 8;
 #       reset = osc3->sync;
};

node submix1 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->fade;
};

node submix2 mixer::fade {
	in0 = osc3->out;
	in1 = osc4->out;
	fade = ionode->fade;
};

node mixer mixer::fade {
	in0 = submix1->out;
	in1 = submix2->out;
	fade = ionode->fade;
};

node filtsuscalc math::mul {
	in0 = vel->out;
	in1 = ionode->fs;
};

node filtenv env::adsr {
	a = ionode->fa;
	d = ionode->fd;
	s = filtsuscalc->out;
	r = ionode->fr;
	trigger = ionode->trigger;
};

node filtmap env::map {
	in = filtenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->cutmin;
	outmax = ionode->cutmax;
};

node filt filt::moog {
	in = mixer->out;
	cutoff = filtmap->out;
	res = ionode->res;
};

node envmixer mixer::mul {
	in0 = filt->out_low;
	in1 = env->out;
};

io ionode;
