name "test";

node ionode {
	channels = 2;
	out0 = lfilt->out;
	out1 = rfilt->out;
	play = 1;

	centerdetune = -0.523;
	ldetune = 0.94;
	rdetune = 1.121;

	centermix = 0.3;
	sidemix = 0.3;

	centerwave1 = 1;
	centerwave2 = 5;
	lwave = 1;
	rwave = 1;

	cutoff = 0.3;
	res = 0.8;

	amp = 0.6;			# Amplitude before filtering
};

node freq misc::midi2freq {
	note = ionode->note;
};

node centerdetune math::add {
	in0 = freq->out;
	in1 = ionode->centerdetune;
};

node ldetune math::add {
	in0 = freq->out;
	in1 = ionode->ldetune;
};

node rdetune math::add {
	in0 = freq->out;
	in1 = ionode->rdetune;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->centerwave1;
};

node osc2 osc::simple {
	freq = centerdetune->out;
	waveform = ionode->centerwave2;
};

node osc3 osc::simple {
	freq = ldetune->out;
	waveform = ionode->lwave;
};

node osc4 osc::simple {
	freq = rdetune->out;
	waveform = ionode->rwave;
};

node centermix mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->centermix;
};

node lmix mixer::fade {
	in0 = osc3->out;
	in1 = centermix->out;
	fade = ionode->sidemix;
};

node rmix mixer::fade {
	in0 = osc4->out;
	in1 = centermix->out;
	fade = ionode->sidemix;
};

node lamp math::mul {
	in0 = lmix->out;
	in1 = ionode->amp;
};

node ramp math::mul {
	in0 = rmix->out;
	in1 = ionode->amp;
};

node lfilt filt::ink {
	in = lamp->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
};

node rfilt filt::ink {
	in = ramp->out;
	cutoff = ionode->cutoff;
	res = ionode->res;
};

io ionode;