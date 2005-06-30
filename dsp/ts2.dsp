# $Id: ts1.dsp 1596 2005-02-01 07:48:46Z misha $
# TS-1
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "TS-1";
author "Leif Ames";
description "Cool Synth";


	@blim = 0.5;
	@blim.widget = 1;
	@blim.min = 0;
	@blim.max = 2;
	@blim.label = "OSC1 Band Limit";

	@pw1 = 0.3;
	@pw1.widget = 1;
	@pw1.min = 0;
	@pw1.max = 1;
	@pw1.label = "OSC1 Pulse Width";
	@pw2 = 0.5;
	@pw2.widget = 1;
	@pw2.min = 0;
	@pw2.max = 1;
	@pw2.label = "OSC2 Pulse Width";

	@fmamt = 1;
	@fmamt.widget = 1;
	@fmamt.min = 0;
	@fmamt.max = 20;
	@fmamt.label = "OSC2 FM Amount";

	@waveform = 2;
	@waveform.widget = 1;
	@waveform.min = 0;
	@waveform.max = 5.1;
	@waveform.label = "OSC2 Waveform";

	@res = 1.5;
	@res.widget = 1;
	@res.min = 0;
	@res.max = 15;
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

	@oscmul = 0.5;
	@oscmul.widget = 1;
	@oscmul.min = 0;
	@oscmul.max = 9;
	@oscmul.label = "OSC2 Multiplier";
	@oscfade = 0.25;
	@oscfade.widget = 1;
	@oscfade.min = 0;
	@oscfade.max = 1;
	@oscfade.label = "Osc Fade";

	@a = 5 ms;
	@a.widget = 1;
	@a.min = 0;
	@a.max = 2000ms;
	@a.label = "Attack";
	@d = 30 ms;
	@d.widget = 1;
	@d.min = 0;
	@d.max = 2000ms;
	@d.label = "Decay";
	@s = 0.75;	# 1 = full, 0 = off
	@s.widget = 1;
	@s.min = 0;
	@s.max = 1;
	@s.label = "Sustain";
	@f = 7000 ms;
	@f.widget = 1;
	@f.min = 0;
	@f.max = 20000ms;
	@f.label = "Falloff";
	@r = 300 ms;
	@r.widget = 1;
	@r.min = 0;
	@r.max = 2000ms;
	@r.label = "Release";


node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;
};

node freq misc::midi2freq {
	note = ionode->note;
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

node env env::adsfr {
	a = @a;
	d = @d;
	s = suscalc->out;
	f = @f;
	r = @r;
	p = ionode->velocity;
	trigger = ionode->trigger;
};

node freqmul math::mul {
	in0 = freq->out;
	in1 = @oscmul;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = cutcalc->out;
	outmax = cutcalc2->out;
};

node map2 math::mul {
	in0 = map1->out;
	in1 = @blim;
};

node osc osc::softsqr {
	freq = freq->out;
	sfreq = map2->out;
	pw = @pw1;
};

node osc2 osc::simple {
	freq = freqmul->out;
	fm = osc->out;
	fmamt = @fmamt;
	waveform = @waveform;
	pw = @pw2;
};

node mixer2 mixer::fade {
	in0 = osc->out;
	in1 = osc2->out;
	fade = @oscfade;
};

node filt filt::res2pole2 {
	in = mixer2->out;
	cutoff = map1->out;
	res = @res;
};

io ionode;
