name "test";

node ionode {
	out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = wav->play;

	ampfalloff = 3.2;
	filtfalloff = 3.6;

	filtmin = 0.05;
	filtmax = 1;
	res = 0.5;

	gatecut = 6;
    gateroll = 3;

	mul1 = 0.5;
	mul2 = 0.5;
	waveform1 = 1;
	waveform2 = 2;
	fmamt = 0.5;
	fmamt2 = 0.5;

	tone = 0.5;
};

node wav input::wav {
};

node gate misc::noisegate {
        in = wav->out;
        rolloff = ionode->gateroll;
        cutoff = ionode->gatecut;
};

node pitch analysis::pitch {
    in = gate->out;
};

node follower env::follower {
        in = gate->out;
        falloff = ionode->ampfalloff;
};

node filtfollower env::followavg {
        in = gate->out;
        falloff = ionode->filtfalloff;
};

node filtmap env::map {
	in = filtfollower->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->filtmin;
	outmax = ionode->filtmax;
};

node osc1 osc::simple {
	freq = pitch->out;
	waveform = ionode->waveform;
	fm = osc2->out;
	fmamt = ionode->fmamt2;
	mul = ionode->mul1;
};

node osc2 osc::simple {
	freq = pitch->out;
	waveform = ionode->waveform;
	fm = osc1->out;
	fmamt = ionode->fmamt;
	mul = ionode->mul2;
};

node premix mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->tone;
};

node filt filt::ink2 {
	in = premix->out;
	cutoff = filtmap->out;
	res = ionode->res;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = follower->out;
};

io ionode;