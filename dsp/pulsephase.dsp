# $Id: pulsephase.dsp,v 1.2 2004/02/09 10:50:28 misha Exp $
name "test";

node ionode {
	out0 = delay->out;
	out1 = delay->out;
	channels = 2;
	play = 1;

	waveform = 1;
	pw = 0.2;

	dlen = 1000;
	dmin = 50;
	dmax = 500;
	dlfo = 0.2;
	dfeed = 0.3;
	ddry = 0.5;

	pulsediv = 6;
	pulsewave = 3;
	pulsepw = 0.2;
};

node freq misc::midi2freq {
	note = ionode->note;
};

node pulsediv math::div {
	in0 = freq->out;
	in1 = ionode->pulsediv;
};

node lfo osc::simple {
	freq = ionode->dlfo;
	waveform = 5;
};

node dmap env::map {
	in = lfo->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->dmin;
	outmax = ionode->dmax;
};

node pulseosc osc::simple {
	freq = pulsediv->out;
	waveform = ionode->pulsewave;
	pw = ionode->pulsepw;
};

node pulsemap env::map {
	in = pulseosc->out;
	inmin = th_min;
	inmax = th_max;
	outmin = 1;
        outmax = 0;
};

node osc osc::simple {
	freq = freq->out;
	waveform = ionode->waveform;
	pw = ionode->pw;
};

node oscmix math::mul {
	in0 = osc->out;
	in1 = pulsemap->out;
};

node delay delay::echo {
	in = oscmix->out;
	size = ionode->dlen;
	delay = dmap->out;
	feedback = ionode->dfeed;
	dry = ionode->ddry;
};

io ionode;
