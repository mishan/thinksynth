name "test";

node ionode {
	channels=2;
	out0 = filt2->out;
	out1 = filt2->out;
	play = 1;

	wave1 = 5;
	wave2 = 1;
	fmamt = 0.8;

	filt1 = 1.23;
	filt2 = 3.21;
	filtlo = 0.3;
	filthi = 1;
	filtres = 0.4;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->wave1;
};

node osc2 osc::simple {
	freq = freq->out;
	waveform = ionode->wave2;
	fm = osc1->out;
	fmamt = ionode->fmamt;
};

node lfo1 osc::simple {
	freq = ionode->filt1;
	waveform = 0;
};

node lfo2 osc::simple {
	freq = ionode->filt2;
	waveform = 0;
};

node lfomap1 env::map {
	in = lfo1->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node lfomap2 env::map {
	in = lfo2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node filt1 filt::moog {
	in = osc2->out;
	cutoff = lfomap1->out;
	res = ionode->filtres;
};

node filt2 filt::ink {
	in = filt1->out_low;
	cutoff = lfomap2->out;
	res = ionode->filtres;
};

io ionode;