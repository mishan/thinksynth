name "test";

# Written by Leif Ames <ink@bespin.org>  5/21/2003
# FM passed through 3 band-pass filters

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = 1;

	f1 = 270;
	f2 = 2290;
	f3 = 3010;

	fade1 = 0.4;
	fade2 = 0.4;

	wave = 4;
	wave2 = 4;
	fmmul = 1;
	fmamt = 0.2;

	res = 0.3;

};

node freq misc::midi2freq {
	note = ionode->note;
};

node fmfreq math::mul {
	in0 = freq->out;
	in1 = ionode->fmmul;
};

node subfreq math::div {
	in0 = freq->out;
	in1 = 2;
};

node mixer mixer::fade {
	in0 = filt1->out;
	in1 = mixer2->out;
	fade = ionode->fade1;
};

node mixer2 mixer::fade {
	in0 = filt2->out_band;
	in1 = filt3->out_band;
	fade = ionode->fade2;
};

node filt1 filt::res2pole {
	in = osc2->out;
	cutoff = ionode->f1;
	res = ionode->res;
};

node filt2 filt::res2pole {
	in = osc2->out;
	cutoff = ionode->f2;
	res = ionode->res;
};

node filt3 filt::res2pole {
	in = osc2->out;
	cutoff = ionode->f3;
	res = ionode->res;
};

node osc1 osc::simple {
	freq = fmfreq->out;
	waveform = ionode->wave2;
};

node osc2 osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
	fm = osc1->out;
	fmamt = ionode->fmamt;
};

io ionode;
