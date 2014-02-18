# Snare Drum 0
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "test";

node ionode {
    out0 = mixer->out;
	out1 = mixer->out;
    channels = 2;
    play = 1;

	tonelen = 2000;
	tonehi = 280;
	tonelow = 250;
	snarelen = 2000;
	snaresample = 0;
	snare = 0.8;
	filter = 0.8;
};

node trlfo osc::simple {
	freq = 0.7;
};

node trlfo2 osc::simple {
	freq = 0.1;
};

node lfofade mixer::fade {
	in0 = trlfo->out;
	in1 = trlfo2->out;
	fade = 0.6;
};

node trigosc osc::simple {
	freq = 8;
	waveform = 2;
	fm = lfofade->out;
	fmamt = 0.9;
	pw = 0.99;
};

node mixer mixer::fade {
	in0 = tonemix->out;
	in1 = snaremix->out;
	fade = ionode->snare;
};

node snaremix mixer::mul {
	in0 = filt->out_high;
	in1 = snareenv->out;
};

node tonemix mixer::mul {
	in0 = osc->out;
	in1 = toneenv->out;
};

node toneenv env::adsr {
	a = 0;
	d = ionode->tonelen;
	s = 0;
	r = 0;
	reset = trigosc->out;
};

node snareenv env::adsr {
	a = 0;
	d = ionode->snarelen;
	s = 80;
	r = 3000;
	reset = trigosc->out;
};

node pitch env::map {
	in = toneenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->tonelow;
	outmax = ionode->tonehi;
};

node filt filt::ds {
	in = dist->out;
	cutoff = ionode->filter;
};

node dist dist::clip {
	in = static->out;
	clip = 0.1;
};

node osc osc::simple {
	freq = pitch->out;
	waveform = 5;
};

node static osc::static {
	sample = ionode->sample;
};

io ionode;
