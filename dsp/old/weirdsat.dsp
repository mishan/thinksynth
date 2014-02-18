name "test";

node ionode {
	channels = 2;
	out0 = dfadel->out;
	out1 = dfader->out;
	play = 1;

	saturation = 3;
	dlen = 800;			# delay length
	dlfo1 = 1.25;		# the first delay
	dlfo2 = 1.47;		# the second
	dflfo = 0.23;		# how fast to pan between delays
	dwave = 5;			# waveform for the delay panning lfo
	dpw = 0.3;			# pw for delay panning lfo

	flfo = 11;			# filter lfo
	fpw = 0.2;			# filter pw
	fwave = 0;			# filter lfo waveform
	filtlo = 0.1;		# lower filter boundry
	filthi = 1;			# upper filter boundry
	res = 0.6;			# filter res

	octfade1 = 0.5;		# mix between main osc and superoscs
	octfade2 = 0.4;		# mix between 1st octave up and 2nd


	waveform = 1;
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
	mul = 0.5;	# one octave up
};

node osc3 osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	mul = 0.25;	# two octaves up
};

node flfo osc::simple {
	freq = ionode->flfo;
	pw = ionode->fpw;
	waveform = ionode->fwave;
};

node dlfo1 osc::simple {
	freq = ionode->dlfo1;
};

node dlfo2 osc::simple {
	freq = ionode->dlfo2;
};

node dflfo osc::simple {
	freq = ionode->dflfo;
	waveform = ionode->dwave;
	pw = ionode->dpw;
};

node fmap env::map {
	in = flfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node dmap1 env::map {
	in = dlfo1->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = ionode->dlen;
};

node dmap2 env::map {
	in = dlfo2->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = ionode->dlen;
};

node dfmapl env::map {
	in = dflfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 0;
	outmax = 1;
};

node dfmapr env::map {
	in = dflfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
	outmax = 0;
};

node octfade1 mixer::fade {
	in0 = osc1->out;
	in1 = octfade2->out;
	fade = ionode->octfade1;
};

node octfade2 mixer::fade {
	in0 = osc2->out;
	in1 = osc3->out;
	fade = ionode->octfade2;
};

node sat dist::inksat {
	in = octfade1->out;
	factor = ionode->saturation;
};

node filter filt::ink {
	in = sat->out;
	cutoff = fmap->out;
	res = ionode->res;
};

node delay1 delay::echo {
	in = filter->out;
	size = ionode->dlen;
	delay = dmap1->out;
	feedback = 0;
	dry = 0;
};

node delay2 delay::echo {
	in = filter->out;
	size = ionode->dlen;
	delay = dmap2->out;
	feedback = 0;
	dry = 0;
};

node dfadel mixer::fade {
	in0 = delay1->out;
	in1 = delay2->out;
	fade = dfmapl->out;
};

node dfader mixer::fade {
	in0 = delay1->out;
	in1 = delay2->out;
	fade = dfmapr->out;
};

io ionode;