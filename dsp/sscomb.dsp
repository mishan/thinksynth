name "test";

node ionode {
	out0 = comb2->out;
	out1 = comb2->out;
    channels = 2;
    play = 1;

	lfo1 = 0.082;
	lfo2 = 0.03;
	sfactorlo = 2;
	sfactorhi = 4;
	bfactorlo = 20;
	bfactorhi = 30;

	cutoff = 0.4;
	res = 0.4;
	shaper = 0.3;

	comb1mul = 0.505;
	comb1feedback = 0.3;
	comb1dry = 0;

	comb2mul = 1.002;
	comb2feedback = 0.7;
	comb2dry = 0;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node combmul math::mul {
	in0 = freq->out;
	in1 = ionode->combmul;
};

node combcalc misc::freq2samples {
	freq = combmul->out;
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

node combmul1 math::mul {
	in0 = freq->out;
	in1 = ionode->comb1mul;
};

node combcalc1 misc::freq2samples {
	freq = combmul1->out;
};

node combmul2 math::mul {
	in0 = freq->out;
	in1 = ionode->comb2mul;
};

node combcalc2 misc::freq2samples {
	freq = combmul2->out;
};

node comb1 delay::echo {
	in = filter->out;
	size = combcalc1->out;
	delay = combcalc1->out;
	feedback = ionode->comb1feedback;
	dry = ionode->comb1dry;
};

node comb2 delay::echo {
	in = comb1->out;
	size = combcalc2->out;
	delay = combcalc2->out;
	feedback = ionode->comb2feedback;
	dry = ionode->comb2dry;
};

io ionode;
