# TS-1
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "Shapeo";
author "Leif Ames";
description "Uses the shapeo osc";

	@shape1 = 0.3;
	@shape1.widget = 1;
	@shape1.min = 0;
	@shape1.max = 12;
	@shape1.label = "Shape 1";
	@shape2 = 3.0;
	@shape2.widget = 1;
	@shape2.min = 0;
	@shape2.max = 12;
	@shape2.label = "Shape 2";

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

	@oscmul = 2;
	@oscmul.widget = 1;
	@oscmul.min = 0;
	@oscmul.max = 9;
	@oscmul.label = "Osc2 Multiplier";
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

node osc osc::shapeo {
	freq = freq->out;
	#sfreq = map2->out;
	shape = @shape1;
};

node osc2 osc::shapeo {
	freq = freqmul->out;
	#sfreq = map2->out;
	shape = @shape2;
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
