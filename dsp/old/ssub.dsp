# $Id$
name "test";

node ionode {
	out0 = filter->out;
	out1 = filter->out;
    channels = 2;
    play = 1;

	lfo1 = 0.082;
	lfo2 = 0.03;
	sfactorlo = 2;
	sfactorhi = 4;
	bfactorlo = 20;
	bfactorhi = 30;

	cutoff = 0.4;
	res = 0.3;
	shaper = 0.3;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node factorlfo1 osc::simple {
	freq = ionode->lfo1;
};

node factorlfo2 osc::simple {
	freq = ionode->lfo2;
};

node factormap1 env::map {
	in = factorlfo1->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->sfactorlo;
	outmax = ionode->sfactorhi;
};

node factormap2 env::map {
	in = factorlfo2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->bfactorlo;
	outmax = ionode->bfactorhi;
};

node osc1 osc::sinsaw {
	freq = freq->out;
	factor = factormap1->out;
};

node osc2 osc::buzzer {
	freq = freq->out;
	factor = factormap2->out;
};

node sub math::sub {
	in0 = osc1->out;
	in1 = osc2->out;
};

node div math::div {
	in0 = sub->out;
	in1 = 2;
};

node filter filt::inkshape {
	in = div->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
	shaper = ionode->shaper;
};

io ionode;
