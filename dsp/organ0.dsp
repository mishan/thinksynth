# $Id$
# Organ Synth
# Leif Ames <ink@bespin.org>
# 6-29-2003

name "Organ 0";
author "Leif Ames";
description "Simple additive organ";


	@vmin = 0.3;
	@vmin.label = "Velocity Minimum";
	@vmin.widget = 1;
	@vmin.min = 0;
	@vmin.max = 1;
	@vmax = 0.7;
	@vmax.label = "Velocity Maximum";
	@vmax.widget = 1;
	@vmax.min = 0;
	@vmax.max = 1;

	@waveform = 5;
	@waveform.label = "Waveform";
	@waveform.widget = 1;
	@waveform.min = 0;
	@waveform.max = 5.5;

	@a = 1ms;
	@a.label = "Attack";
	@a.widget = 1;
	@a.min = 0;
	@a.max = 1000ms;
	@d = 3ms;
	@d.label = "Decay";
	@d.widget = 1;
	@d.min = 0;
	@d.max = 1000ms;
	@s = 0.5;
	@s.label = "Sustain";
	@s.widget = 1;
	@s.min = 0;
	@s.max = 1;
	@r = 5ms;
	@r.label = "Release";
	@r.widget = 1;
	@r.min = 0;
	@r.max = 1000ms;

	@mul1 = 2;
	@mul1.label = "Drawbar 2 Pitch";
	@mul1.widget = 1;
	@mul1.min = 0;
	@mul1.max = 16;
	@mul2 = 4;
	@mul2.label = "Drawbar 3 Pitch";
	@mul2.widget = 1;
	@mul2.min = 0;
	@mul2.max = 16;
	@mul3 = 8;
	@mul3.label = "Drawbar 4 Pitch";
	@mul3.widget = 1;
	@mul3.min = 0;
	@mul3.max = 16;


node ionode {
	channels = 2;
	out0 = envmixer->out;
	out1 = envmixer->out;
	play = env->play;

	#vmin = 0.01;
	#vmax = 1;

	fade = vmap->out;
	#waveform = 5;

	#a = 200;
	#d = 800;
	#s = 0.5;
	#r = 1000;
};

node suscalc math::mul {
	in0 = ionode->velocity;
	in1 = @s;
};

node env env::adsr {
	a = @a;
	d = @d;
	s = suscalc->out;
	r = @r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node vmap env::map {
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = @vmin;
	outmax = @vmax;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = @waveform;
};

node osc2 osc::simple {
        freq = freq->out;
        waveform = @waveform;
	mul = @mul1;
#	reset = osc1->sync;
};

node osc3 osc::simple {
        freq = freq->out;
        waveform = @waveform;
        mul = @mul2;
#        reset = osc2->sync;
};

node osc4 osc::simple {
        freq = freq->out;
        waveform = @waveform;
        mul = @mul3;
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
