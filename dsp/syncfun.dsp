# $Id: syncfun.dsp,v 1.1 2004/12/07 02:38:57 ink Exp $
name "Sync Fun";
author "Leif Ames";
description "A little bit of fun with hard-sync";

	@a = 0.7 ms;
	@a.widget = 1;
	@a.min = 0;
	@a.max = 2000ms;
	@a.label = "Attack";
	@d = 100 ms;
	@d.widget = 1;
	@d.min = 0;
	@d.max = 2000ms;
	@d.label = "Decay";
	@s = 0.3;	# 1 = full, 0 = off
	@s.widget = 1;
	@s.min = 0;
	@s.max = 1;
	@s.label = "Sustain";
	@r = 100 ms;
	@r.widget = 1;
	@r.min = 0;
	@r.max = 5000ms;
	@r.label = "Release";

	@syncdetune = 2.9;
	@syncdetune.widget = 1;
	@syncdetune.min = 0;
	@syncdetune.max = 8;
	@syncdetune.label = "Sync Detune Multiplier";

	@syncmix = 0.7;
	@syncmix.widget = 1;
	@syncmix.min = 0;
	@syncmix.max = 1;
	@syncmix.label = "Sync Mix";

	@wave1 = 4;
	@wave1.widget = 1;
	@wave1.min = 0;
	@wave1.max = 5.1;
	@wave1.label = "Waveform 1";

	@wave2 = 3;
	@wave2.widget = 1;
	@wave2.min = 0;
	@wave2.max = 5.1;
	@wave2.label = "Waveform 2";

	@res = 0.8;
	@res.widget = 1;
	@res.min = 0;
	@res.max = 1;
	@res.label = "Resonance";
	@fmin = 0.06;
	@fmin.widget = 1;
	@fmin.min = 0;
	@fmin.max = 1;
	@fmin.label = "Cutoff Low";
	@fmax = 0.9;
	@fmax.widget = 1;
	@fmax.min = 0;
	@fmax.max = 1;
	@fmax.label = "Cutoff High";

node ionode {
	channels = 2;
	out0 = envmixer->out;
	out1 = envmixer->out;
	play = env->play;

	amp = 0.5;			# Amplitude before filtering
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
	outmin = @fmin;
	outmax = @fmax;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = @wave1;
};

node osc2 osc::simple {
	freq = freq->out;
	waveform = @wave2;
	reset = osc1->sync;
	mul = @syncdetune;
};

node mix mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = @syncmix;
};

node filt filt::ink {
	in = mix->out;
	cutoff = fmap->out;
	res = @res;
};

node envmixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

io ionode;