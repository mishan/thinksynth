# Organ Synth
# Leif Ames <ink@bespin.org>
# 6-29-2003

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

	a = 200;
	d = 800;
	s = 0.5;
	r = 1000;
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

node vmap env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->vmin;
	outmax = ionode->vmax;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
};

node osc2 osc::simple {
        freq = freq->out;
        waveform = ionode->waveform;
	mul = 2;
#	reset = osc1->sync;
};

node osc3 osc::simple {
        freq = freq->out;
        waveform = ionode->waveform;
        mul = 4;
#        reset = osc2->sync;
};

node osc4 osc::simple {
        freq = freq->out;
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

node envmixer mixer::mul {
	in0 = mixer->out;
	in1 = env->out;
};

io ionode;
