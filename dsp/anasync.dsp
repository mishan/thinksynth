# $Id$

name "AnaSync";
author "Leif Ames";
description "Uses hard sync to add extra resonant formant";

	@a = 4 ms;
	@a.widget = 1;
	@a.min = 0;
	@a.max = 2000ms;
	@a.label = "Attack";
	@d = 200 ms;
	@d.widget = 1;
	@d.min = 0;
	@d.max = 2000ms;
	@d.label = "Decay";
	@s = 0.8;	# 1 = full, 0 = off
	@s.widget = 1;
	@s.min = 0;
	@s.max = 1;
	@s.label = "Sustain";
	@r = 100 ms;
	@r.widget = 1;
	@r.min = 0;
	@r.max = 5000ms;
	@r.label = "Release";

	@syncmix = 0.2;
	@syncmix.widget = 1;
	@syncmix.min = 0;
	@syncmix.max = 1;
	@syncmix.label = "Sync Mix";

	@wave1 = 1;
	@wave1.widget = 1;
	@wave1.min = 0;
	@wave1.max = 5.1;
	@wave1.label = "Waveform 1";

	@wave2 = 3;
	@wave2.widget = 1;
	@wave2.min = 0;
	@wave2.max = 5.1;
	@wave2.label = "Waveform 2";

	@res = 3;
	@res.widget = 1;
	@res.min = 0;
	@res.max = 10;
	@res.label = "Resonance";
	@cutoff = 4;
	@cutoff.widget = 1;
	@cutoff.min = 0;
	@cutoff.max = 16;
	@cutoff.label = "Cutoff";
	@famt = 3;
	@famt.widget = 1;
	@famt.min = 0;
	@famt.max = 8;
	@famt.label = "Filter Envelope";

node ionode {
	channels = 2;
	out0 = envmixer->out;
	out1 = envmixer->out;
	play = env->play;
};

node cutcalc math::mul {
	in0 = freq->out;
	in1 = @cutoff;
};

node cutcalc2 math::mul {	# multiply the filter start by famt
	in0 = cutcalc->out;
	in1 = @famt;
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

node fmap env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = cutcalc->out;
	outmax = cutcalc2->out;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = @wave1;
};

node osc2 osc::simple {
	freq = fmap->out;
	waveform = @wave2;
	reset = osc1->sync;
};

node mix mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = @syncmix;
};

node filt filt::res2pole {
	in = mix->out;
	cutoff = fmap->out;
	res = @res;
};

node envmixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

io ionode;