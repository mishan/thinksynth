# $Id: amb01.dsp,v 1.3 2004/10/01 10:05:32 ink Exp $
name "AMB 01";
author "Leif Ames";
description "4 oscs in a stereo mix";

	@a = 0.7 ms;
	@a.widget = 1;
	@a.min = 0;
	@a.max = 2000ms;
	@a.label = "Attack";
	@d = 50 ms;
	@d.widget = 1;
	@d.min = 0;
	@d.max = 2000ms;
	@d.label = "Decay";
	@s = 0.7;	# 1 = full, 0 = off
	@s.widget = 1;
	@s.min = 0;
	@s.max = 1;
	@s.label = "Sustain";
	@r = 100 ms;
	@r.widget = 1;
	@r.min = 0;
	@r.max = 5000ms;
	@r.label = "Release";

	@centerdetune = -0.523;
	@centerdetune.widget = 1;
	@centerdetune.min = -10;
	@centerdetune.max = 10;
	@centerdetune.label = "Center Detune";
	@ldetune = 0.94;
	@ldetune.widget = 1;
	@ldetune.min = -10;
	@ldetune.max = 10;
	@ldetune.label = "Left Detune";
	@rdetune = 1.121;
	@rdetune.widget = 1;
	@rdetune.min = -10;
	@rdetune.max = 10;
	@rdetune.label = "Right Detune";

	@centermix = 0.3;
	@centermix.widget = 1;
	@centermix.min = 0;
	@centermix.max = 1;
	@centermix.label = "Center Mix";
	@sidemix = 0.3;
	@sidemix.widget = 1;
	@sidemix.min = 0;
	@sidemix.max = 1;
	@sidemix.label = "Side Mix";

	@centerwave1 = 1;
	@centerwave1.widget = 1;
	@centerwave1.min = 0;
	@centerwave1.max = 5.1;
	@centerwave1.label = "Center Waveform 1";
	@centerwave2 = 5;
	@centerwave2.widget = 1;
	@centerwave2.min = 0;
	@centerwave2.max = 5.1;
	@centerwave2.label = "Center Waveform 2";
	@lwave = 1;
	@lwave.widget = 1;
	@lwave.min = 0;
	@lwave.max = 5.1;
	@lwave.label = "Left Waveform";
	@rwave = 1;
	@rwave.widget = 1;
	@rwave.min = 0;
	@rwave.max = 5.1;
	@rwave.label = "Right Waveform";

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
	out0 = lmixer->out;
	out1 = rmixer->out;
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
	in = ionode->velocity;
	inmin = 0;
	inmax = th_max;
	outmin = @fmin;
	outmax = @fmax;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node centerdetune math::add {
	in0 = freq->out;
	in1 = @centerdetune;
};

node ldetune math::add {
	in0 = freq->out;
	in1 = @ldetune;
};

node rdetune math::add {
	in0 = freq->out;
	in1 = @rdetune;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = @centerwave1;
};

node osc2 osc::simple {
	freq = centerdetune->out;
	waveform = @centerwave2;
};

node osc3 osc::simple {
	freq = ldetune->out;
	waveform = @lwave;
};

node osc4 osc::simple {
	freq = rdetune->out;
	waveform = @rwave;
};

node centermix mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = @centermix;
};

node lmix mixer::fade {
	in0 = centermix->out;
	in1 = osc3->out;
	fade = @sidemix;
};

node rmix mixer::fade {
	in0 = centermix->out;
	in1 = osc4->out;
	fade = @sidemix;
};

node lamp math::mul {
	in0 = lmix->out;
	in1 = ionode->amp;
};

node ramp math::mul {
	in0 = rmix->out;
	in1 = ionode->amp;
};

node lfilt filt::ink2 {
	in = lamp->out;
	cutoff = fmap->out;
	res = @res;
};

node rfilt filt::ink2 {
	in = ramp->out;
	cutoff = fmap->out;
	res = @res;
};

node lmixer mixer::mul {
	in0 = lfilt->out;
	in1 = env->out;
};

node rmixer mixer::mul {
	in0 = rfilt->out;
	in1 = env->out;
};

io ionode;