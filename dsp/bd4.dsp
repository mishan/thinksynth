name "test";

node ionode {
	out0 = fade3->out;
	out1 = fade3->out;
	channels = 2;
	play = aenv1->play;

	fmin = 20;
	fmax = 240;
	toned = 2500;
	tonemid = 50;
	tonerelease = 10000;
	ampd = 7000;
	ampmid = 120;
	release = 5000;

	tonemul = 1.03; # How much higher than the last
	dmul = 0.8; # How much shorter than the last
	fade1 = 0.3; # Osc 1 and 2
	fade2 = 0.3; # Osc 3 and 4
	fade3 = 0.3; # Fade 1 and 2
};

node mixer1 mixer::mul {
	in0 = osc1->out;
	in1 = aenv1->out;
};

node mixer2 mixer::mul {
	in0 = osc2->out;
	in1 = aenv2->out;
};

node mixer3 mixer::mul {
	in0 = osc3->out;
	in1 = aenv3->out;
};

node mixer4 mixer::mul {
	in0 = osc4->out;
	in1 = aenv4->out;
};

node tonerelease math::add {  # tone envelope should be longer than amp
	in0 = ionode->release;
	in1 = ionode->ampd;
};

node dmul1 math::mul {
	in0 = ionode->release;
	in1 = ionode->dmul;
};

node dmul2 math::mul {
	in0 = dmul1->out;
	in1 = ionode->dmul;
};

node dmul3 math::mul {
	in0 = dmul2->out;
	in1 = ionode->dmul;
};

node tenv env::adsr {
	a = 5;
	d = ionode->toned;
	s = ionode->tonemid;
	r = tonerelease->out;
	trigger = 0;
};

node aenv1 env::adsr {
        a = 5;
        d = ionode->ampd;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node aenv2 env::adsr {
        a = 5;
        d = dmul1->out;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node aenv3 env::adsr {
        a = 5;
        d = dmul2->out;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node aenv4 env::adsr {
        a = 0;
        d = dmul3->out;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node map1 env::map {
	in = tenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->fmin;
	outmax = ionode->fmax;
};

node pitchcalc1 math::mul {
	in0 = map1->out;
	in1 = ionode->tonemul;
};

node pitchcalc2 math::mul {
	in0 = pitchcalc1->out;
	in1 = ionode->tonemul;
};

node pitchcalc3 math::mul {
	in0 = pitchcalc2->out;
	in1 = ionode->tonemul;
};

node osc1 osc::simple {
	freq = map1->out;
	waveform = 0;
};

node osc2 osc::simple {
	freq = map1->out;
	waveform = 0;
	fm = pitchcalc1->out;
	fmamt = 1;
};

node osc3 osc::simple {
	freq = map1->out;
	waveform = 0;
	fm = pitchcalc2->out;
	fmamt = 1;
};

node osc4 osc::simple {
	freq = map1->out;
	waveform = 0;
	fm = pitchcalc3->out;
	fmamt = 1;
};

node fade1 mixer::fade {
	in0 = mixer1->out;
	in1 = mixer2->out;
	fade = ionode->fade1;
};

node fade2 mixer::fade {
	in0 = mixer3->out;
	in1 = mixer4->out;
	fade = ionode->fade2;
};

node fade3 mixer::fade {
	in0 = fade1->out;
	in1 = fade2->out;
	fade = ionode->fade3;
};

io ionode;
