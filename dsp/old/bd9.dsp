# $Id: bd9.dsp,v 1.1 2004/09/15 06:07:43 ink Exp $
name "test";

node ionode {
	out0 = filt->out;
	out1 = filt->out;
	channels = 2;
	play = aenv1->play;

	notemin = 10;
	notemax = 52;
	toned = 4500;
	tonemid = 100;
	ampd = 6000;
	ampmid = 200;
	release = 3000;

	cutoff = 0.25;
	res = 0.95;
	shaper = 2;

	toneadd = 4; # How much to add for each osc (in halfsteps)
	dmul = 0.3; # How much shorter than the last

	fade1 = 0.4; # Osc 1 and 2
	fade2 = 0.4; # Osc 3 and 4
	fade3 = 0.4; # Osc 4 and 6
	fade4 = 0.4; # Fader 2 and 3
	fade5 = 0.33; # Fader 1 and 4

	waveform = 5;
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

node mixer5 mixer::mul {
	in0 = osc5->out;
	in1 = aenv5->out;
};

node mixer6 mixer::mul {
	in0 = osc6->out;
	in1 = aenv6->out;
};

node tonerelease math::add {  # tone envelope should be longer than amp
	in0 = ionode->release;
	in1 = ionode->ampd;
};

node dmul1 math::mul {
	in0 = ionode->ampd;
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

node dmul4 math::mul {
	in0 = dmul3->out;
	in1 = ionode->dmul;
};

node dmul5 math::mul {
	in0 = dmul4->out;
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
        a = 0;
        d = ionode->ampd;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node aenv2 env::adsr {
        a = 0;
        d = dmul1->out;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node aenv3 env::adsr {
        a = 0;
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

node aenv5 env::adsr {
        a = 0;
        d = dmul4->out;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node aenv6 env::adsr {
        a = 0;
        d = dmul5->out;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node map1 env::map {
	in = tenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->notemin;
	outmax = ionode->notemax;
};

node toneaddcalc1 math::add {
	in0 = map1->out;
	in1 = ionode->toneadd;
};

node toneaddcalc2 math::add {
	in0 = toneaddcalc1->out;
	in1 = ionode->toneadd;
};

node toneaddcalc3 math::add {
	in0 = toneaddcalc2->out;
	in1 = ionode->toneadd;
};

node toneaddcalc4 math::add {
	in0 = toneaddcalc3->out;
	in1 = ionode->toneadd;
};

node toneaddcalc5 math::add {
	in0 = toneaddcalc4->out;
	in1 = ionode->toneadd;
};

node midi2freq1 misc::midi2freq {
	note = map1->out;
};

node midi2freq2 misc::midi2freq {
	note = toneaddcalc1->out;
};

node midi2freq3 misc::midi2freq {
	note = toneaddcalc2->out;
};

node midi2freq4 misc::midi2freq {
	note = toneaddcalc3->out;
};

node midi2freq5 misc::midi2freq {
	note = toneaddcalc4->out;
};

node midi2freq6 misc::midi2freq {
	note = toneaddcalc5->out;
};

node osc1 osc::simple {
	freq = midi2freq1->out;
	waveform = ionode->waveform;
};

node osc2 osc::simple {
	freq = midi2freq2->out;
	waveform = ionode->waveform;
};

node osc3 osc::simple {
	freq = midi2freq3->out;
	waveform = ionode->waveform;
};

node osc4 osc::simple {
	freq = midi2freq4->out;
	waveform = ionode->waveform;
};

node osc5 osc::simple {
	freq = midi2freq5->out;
	waveform = ionode->waveform;
};

node osc6 osc::simple {
	freq = midi2freq6->out;
	waveform = ionode->waveform;
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
	in0 = mixer5->out;
	in1 = mixer6->out;
	fade = ionode->fade3;
};

node fade4 mixer::fade {
	in0 = fade2->out;
	in1 = fade3->out;
	fade = ionode->fade4;
};

node fade5 mixer::fade {
	in0 = fade1->out;
	in1 = fade4->out;
	fade = ionode->fade5;
};

node filt filt::inkshape {
	in = fade5->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
	shaper = ionode->shaper;
};

io ionode;
