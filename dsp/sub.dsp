# Sub -- a sine an octave under everything, and nothing else.
#
# The bass of a dub record is felt more than heard: a sine low enough
# that a small speaker barely draws it, and steady enough that it never
# competes with the kick for the same moment. `Harmonic' adds its second
# harmonic, which is what lets a laptop's speaker suggest a note its
# cone cannot move for; `Drive' rounds it into the gentle square a desk
# pushed too hard makes of it.
#
# MONO, AND IT GLIDES. One voice slides from note to note over `Glide',
# because a sub line is one string, and a chord in the sub is mud.

name "Sub";
author "Misha Nasledov";
description "A mono sine sub-bass with a second harmonic, drive and glide.";
category "Bass";

    @harmonic = 0.15;
    @harmonic.widget = 1;
    @harmonic.min = 0;
    @harmonic.max = 0.6;
    @harmonic.label = "Harmonic";

    @drive = 1.2;
    @drive.widget = 1;
    @drive.min = 0.5;
    @drive.max = 4;
    @drive.label = "Drive";

    @glide = 60 ms;
    @glide.widget = 1;
    @glide.min = 0ms;
    @glide.max = 500ms;
    @glide.label = "Glide";

    @a = 8 ms;
    @a.widget = 1;
    @a.min = 1ms;
    @a.max = 500ms;
    @a.label = "Attack";

    @r = 150 ms;
    @r.widget = 1;
    @r.min = 70ms;
    @r.max = 2000ms;
    @r.label = "Release";

    @level = 0.8;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 1;
    mono = 1;
    out0 = vca->out;
    play = env->play;
};

node pitch misc::midi2freq {
    note = ionode->note;
};

node freq misc::slew {
    in = pitch->out;
    time = @glide;
};

node fund osc::simple {
    freq = freq->out;
    waveform = 0;
};

node second osc::simple {
    freq = freq->out * 2;
    waveform = 0;
};

node drive dist::saturate {
    in = fund->out + second->out * @harmonic;
    factor = @drive;
};

node env env::adsr {
    a = @a;
    d = 1 ms;
    s = 1;
    r = @r;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = drive->out * @level * (0.5 + 0.5 * ionode->velocity / th_max);
    in1 = env->out;
};

io ionode;
