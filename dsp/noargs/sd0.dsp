# $Id$
# Snare Drum 0
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = snareenv->play;

	tonelen = 50 ms;
	tonehi = 280;
	tonelow = 250;
	snarelen = 50 ms;
	snares = 80%;
	snarer = 65 ms;
	snaresample = 0;
	snare = 0.8;
	filter = 0.8;
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
	p = ionode->velocity;
	trigger = 0;
};

node snareenv env::adsr {
	a = 0;
	d = ionode->snarelen;
	s = ionode->snares;
	r = ionode->snarer;
	p = ionode->velocity;
	trigger = 0;
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
