# $Id$
# Written by Leif Ames <ink@bespin.org>  5/21/2003
# FM passed through 3 band-pass filters
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = 1;

	f1 = 400;
	f2 = 2250;
	f3 = 3300;

	fade1 = 0.3;
	fade2 = 0.5;

	wave = 1;
	pw = 0.4 ;
	detune1 = 1.2;
	detune2 = -0.8;
	detune3 = 2.1;

	oscfade1 = 0.2;
	oscfade2 = 0.2;
	oscfade3 = 0.3;

	res = 5;

};

node freq misc::midi2freq {
	note = ionode->note;
};

node detune1 math::add {
	in0 = freq->out;
	in1 = ionode->detune1;
};

node detune2 math::add {
	in0 = freq->out;
	in1 = ionode->detune2;
};

node detune3 math::add {
	in0 = freq->out;
	in1 = ionode->detune3;
};

node mixer mixer::fade {
	in0 = filt1->out_band;
	in1 = mixer2->out;
	fade = ionode->fade1;
};

node mixer2 mixer::fade {
	in0 = filt2->out_band;
	in1 = filt3->out_band;
	fade = ionode->fade2;
};

node filt1 filt::res2pole {
	in = oscmix3->out;
	cutoff = ionode->f1;
	res = ionode->res;
};

node filt2 filt::res2pole {
	in = oscmix3->out;
	cutoff = ionode->f2;
	res = ionode->res;
};

node filt3 filt::res2pole {
	in = oscmix3->out;
	cutoff = ionode->f3;
	res = ionode->res;
};

node oscmix1 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->oscfade1;
};

node oscmix2 mixer::fade {
	in0 = osc3->out;
	in1 = osc4->out;
	fade = ionode->oscfade2;
};

node oscmix3 mixer::fade {
	in0 = oscmix1->out;
	in1 = oscmix2->out;
	fade = ionode->oscfade3;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

node osc2 osc::simple {
	freq = detune1->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

node osc3 osc::simple {
	freq = detune2->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

node osc4 osc::simple {
	freq = detune3->out;
	waveform = ionode->wave;
	pw = ionode->pw;
};

io ionode;
