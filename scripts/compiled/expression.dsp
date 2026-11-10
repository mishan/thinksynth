# compiledcheck's: what no shipped video patch reads -- the velocity, the
# first aux and a choke -- in a picture compiled for the engine, with an
# envelope short enough that voices finish and come back from the pool.
name "Expression";
category "Experiments";

    @in_level = 0;

node ionode {
    channels = 3;
    choke = 1;
    out0 = r->out;
    out1 = g->out;
    out2 = b->out;
    play = env->play;
};

node s misc::scan { };
node f misc::midi2freq { note = ionode->note; };
node env env::adsr { a = 5 ms; d = 30 ms; s = 0.7; r = 40 ms; p = 1;
                     trigger = ionode->trigger; };
node wave math::sin { index = s->x * f->out * 0.05 - s->t; wavelength = 1; };

node r math::mul { in0 = wave->out * ionode->velocity; in1 = env->out; };
node g math::mul { in0 = s->y * ionode->aux0; in1 = env->out; };
node b math::mul { in0 = wave->out * @in_level; in1 = env->out; };

io ionode;
