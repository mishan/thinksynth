# Snare Drum 0
# Leif Ames <ink@bespin.org>
# 5-11-2003

name "test";

node ionode test::test {
    out0 = mixer->out;
	out1 = mixer->out;
    channels = 2;
    play = snareenv->play;

	tonelen = 2000;
	tonehi = 280;
	tonelow = 250;
	snarelen = 4000;
	snaresample = 0;
	snare = 0.5;
	filter = 0.4;
	res = 0.3;
    };
node mixer mixer::fade {
	in0 = tonemix->out;
	in1 = snaremix->out;
	fade = ionode->snare;
	};
node snaremix mixer::mul {
	in0 = filt->highout;
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
	trigger = 0;
	};
node snareenv env::adsr {
	a = 0;
	d = ionode->snarelen;
	s = 0;
	r = 0;
	trigger = 0;
	};
node pitch env::map {
	in = toneenv->out;
	inmin = 0;
	inmax = th_max;
	outmin = ionode->tonelow;
	outmax = ionode->tonehi;
	};
node filt filt::rds {
	in = static->out;
	cutoff = ionode->filter;
	res = ionode->res;
	};
node osc osc::simple {
	freq = pitch->out;
	waveform = 4;
	};
node static osc::static {
	sample = ionode->sample;
	};

io ionode;
