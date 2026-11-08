# The video synth for video.gen: a gray ramp, black at the left edge, for
# as long as a note is held, and nothing after.

name "videoramp";

node ionode {
    channels = 3;
    out0 = lit->out;
    out1 = lit->out;
    out2 = lit->out;
    play = env->play;
};

node s misc::scan { };

node env env::adsr { a = 0; d = 0; s = 1; r = 0; p = 1; trigger = ionode->trigger; };

node lit math::mul { in0 = s->x; in1 = env->out; };

io ionode;
