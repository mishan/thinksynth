# Organ Synth
# Leif Ames <ink@bespin.org>
# 6-29-2003

name "test";

node ionode {
	channels = 2;
	out0 = mixer->out;
	out1 = mixer->out;
	play = 1;

	fade = 0.4;
	waveform = 5;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node osc1 osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
};

node osc2 osc::simple {
        freq = freq->out;
        waveform = ionode->waveform;
	mul = 0.5;
#	reset = osc1->sync;
};

node osc3 osc::simple {
        freq = freq->out;
        waveform = ionode->waveform;
        mul = 0.25;
#        reset = osc2->sync;
};

node osc4 osc::simple {
        freq = freq->out;
        waveform = ionode->waveform;
        mul = 0.125;
 #       reset = osc3->sync;
};

node submix1 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->fade;
};

node submix2 mixer::fade {
	in0 = osc3->out;
	in1 = osc4->out;
	fade = ionode->fade;
};

node mixer mixer::fade {
	in0 = submix1->out;
	in1 = submix2->out;
	fade = ionode->fade;
};

io ionode;
