# $Id: sorg.dsp,v 1.1 2004/12/07 02:38:57 ink Exp $
name "test";

node ionode {
	out0 = mixer->out;
	out1 = mixer->out;
	channels = 2;
	play = env->play;

	a = 1000;
	d = 10000;

	filtlo = 0.01;
	filthi = 0.95;
	res = 0.8;

	shapelow = 3;
	shapehi = 0.4;

	pw1 = 0.1;
	pw2 = 0.1;
	pw3 = 0.05;
	freq2mul = 2.01;
	freq3mul = 4.015;

	mix1 = 0.6;	# osc 1 and osc2
	mix2 = 0.2;	# mix1 and osc3
};

node freq misc::midi2freq {
	note = ionode->note;
};

node freq2 math::mul {
	in0 = freq->out;
	in1 = ionode->freq2mul;
};

node freq3 math::mul {
        in0 = freq->out;
	in1 = ionode->freq3mul;
};

node mixer mixer::mul {
	in0 = filt->out;
	in1 = env->out;
};

node env env::ad {
	a = ionode->a;
	d = ionode->d;
	trigger = 0;
};

node map1 env::map {
	in = env->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->filtlo;
	outmax = ionode->filthi;
};

node map2 env::map {
	in = env->out;
	inmin = th_min;
	inmax = th_max;
	outmin = ionode->shapelo;
	outmax = ionode->shapehi;
};

node filt filt::ink {
	in = mix2->out;
	cutoff = map1->out;
	res = ionode->res;
};

node osc1 osc::shapeo {
	freq = freq->out;
	shape = map2->out;
	pw = 0.2;
};

node osc2 osc::shapeo {
        freq = freq2->out;
        shape = map2->out;
        pw = 0.2;
};

node osc3 osc::shapeo {
        freq = freq3->out;
        shape = map2->out;
        pw = 0.2;
};

node mix1 mixer::fade {
	in0 = osc1->out;
	in1 = osc2->out;
	fade = ionode->mix1;
};

node mix2 mixer::fade {
        in0 = mix1->out;
        in1 = osc3->out;
        fade = ionode->mix2;
};

io ionode;
