/*
 * Copyright (C) 2004-2026 The thinksynth authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * hidblockcheck -- what a HID mapping block says, held still.
 *
 * One table. Each row is a profile and either the block it should read as,
 * written out by describe() below, or the line and the words of the error
 * it should be refused with. A refusal is checked for its line as well as
 * its message, because a profile is edited by hand and "somewhere in this
 * file" is not an answer.
 */

#include <cstdio>
#include <sstream>
#include <string>

#include "thcHidBlock.h"

using std::string;

static int failed = 0;

static string
describe (const thcHidBinding &b)
{
    std::ostringstream s;

    s << b.control << ":";

    switch (b.kind)
    {
    case thcHidBinding::NOTE:     s << "note " << b.semis; break;
    case thcHidBinding::HOLD:     s << "hold " << b.layer; break;
    case thcHidBinding::TOGGLE:   s << "toggle " << b.layer; break;
    case thcHidBinding::OCTAVE:   s << "octave " << b.semis; break;
    case thcHidBinding::VALUE:    s << "value " << b.arg; break;
    case thcHidBinding::PITCH:
        s << "pitch " << (b.octaves ? "octave" : "semitone") << " " << b.range;
        break;
    case thcHidBinding::VELOCITY: s << "velocity"; break;
    case thcHidBinding::STRIKE:
        s << "strike " << b.semis << " at" << b.at << " off" << b.off;
        break;
    }

    if (b.channel)
        s << " ch" << b.channel;
    if (b.hasMin)
        s << " min" << b.min;
    if (b.hasMax)
        s << " max" << b.max;
    if (b.kind == thcHidBinding::VALUE && b.shape == thcHidBinding::EXP)
        s << " exp";
    if (b.center)
        s << " center" << b.center;
    if (b.smooth)
        s << " smooth" << b.smooth;
    if (b.latch)
        s << " latch";

    s << " @" << b.line;

    return s.str();
}

static string
describe (const thcHidBlock &blk)
{
    std::ostringstream s;

    s << blk.device << " ch" << blk.channel << " base" << blk.base
      << " vel" << blk.velocity;

    for (size_t i = 0; i < blk.bindings.size(); i++)
        s << " | " << describe(blk.bindings[i]);

    for (size_t i = 0; i < blk.layers.size(); i++)
    {
        const thcHidLayer &l = blk.layers[i];

        s << " | layer " << l.name << " ch" << l.channel << " base" << l.base
          << " @" << l.line;
        for (size_t j = 0; j < l.when.size(); j++)
            s << (j ? "," : " when ") << l.when[j].first << ">="
              << l.when[j].second;
        s << " [";

        for (size_t j = 0; j < l.bindings.size(); j++)
            s << (j ? " | " : "") << describe(l.bindings[j]);

        s << "]";
    }

    return s.str();
}

#define ZEROS10  "0000000000"
#define ZEROS100 ZEROS10 ZEROS10 ZEROS10 ZEROS10 ZEROS10 \
                 ZEROS10 ZEROS10 ZEROS10 ZEROS10 ZEROS10
#define ZEROS400 ZEROS100 ZEROS100 ZEROS100 ZEROS100

struct Row
{
    const char *name;
    const char *text;
    const char *want;     /* describe(), or NULL when it should be refused */
    int         errLine;  /* where the refusal is */
    const char *errHas;   /* words the refusal must contain */
};

static const Row ROWS[] = {
    { "empty profile",
      "device index { };",
      "index ch0 base60 vel100", 0, NULL },

    { "the plan's sketch",
      "device index {\n"
      "    base = \"C4\";\n"
      "    channel = 1;\n"
      "    velocity = 90;\n"
      "    stick_y = pitch { step = octave; range = 2; };\n"
      "    a = note 0;  b = note 2;  thumbstick = note 4;\n"
      "    trigger = velocity { min = 30; max = 127; };\n"
      "    grip = hold mod;\n"
      "    system = toggle drone;\n"
      "    middle = \"cutoff\" { min = 200; max = 8000; shape = exp; };\n"
      "    layer mod {\n"
      "        middle = \"resonance\";\n"
      "        a = octave -1;  b = octave 1;\n"
      "    };\n"
      "    layer drone {\n"
      "        channel = 2;\n"
      "    };\n"
      "};\n",
      "index ch1 base60 vel90"
      " | stick_y:pitch octave 2 @5"
      " | a:note 0 @6 | b:note 2 @6 | thumbstick:note 4 @6"
      " | trigger:velocity min30 max127 @7"
      " | grip:hold mod @8"
      " | system:toggle drone @9"
      " | middle:value cutoff min200 max8000 exp @10"
      " | layer mod ch0 base-1 @11"
      " [middle:value resonance @12 | a:octave -1 @13 | b:octave 1 @13]"
      " | layer drone ch2 base-1 @15 []", 0, NULL },

    { "a note below the base",
      "device index { a = note -12; };",
      "index ch0 base60 vel100 | a:note -12 @1", 0, NULL },

    { "a sharp in the base note",
      "device index { base = \"C#4\"; };",
      "index ch0 base61 vel100", 0, NULL },

    { "a note on its own channel",
      "device index { a = note 0 { channel = 3; }; };",
      "index ch0 base60 vel100 | a:note 0 ch3 @1", 0, NULL },

    { "a value with half a range, on an effect",
      "device index { ring = \"fx.mix\" { max = 0.5; channel = 2; }; };",
      "index ch0 base60 vel100 | ring:value fx.mix ch2 max0.5 @1", 0, NULL },

    { "a pitch axis in semitones by default",
      "device index { pad_y = pitch { range = 12; }; };",
      "index ch0 base60 vel100 | pad_y:pitch semitone 12 @1", 0, NULL },

    { "a negative min, signed by the tokenizer",
      "device index { m = \"cutoff\" { min = -5; max = 5; }; };",
      "index ch0 base60 vel100 | m:value cutoff min-5 max5 @1", 0, NULL },

    { "a velocity source with no range",
      "device index { index = velocity; };",
      "index ch0 base60 vel100 | index:velocity @1", 0, NULL },

    { "a strike, with its own thresholds and velocity",
      "device index { grip_top = strike 7 { at = 0.7; off = 0.5; min = 20; "
      "max = 110; channel = 2; }; };",
      "index ch0 base60 vel100 | grip_top:strike 7 at0.7 off0.5 ch2 min20 "
      "max110 @1", 0, NULL },
    { "a strike's off is under its at by default",
      "device index { trigger = strike -5 { at = 0.9; }; };",
      "index ch0 base60 vel100 | trigger:strike -5 at0.9 off0.7 @1", 0, NULL },
    { "one axis on several knobs and a pitch axis, from a center",
      "device index { grip_force = \"cutoff\"; grip_force = \"drive\"; "
      "tilt = pitch { step = octave; range = 1; center = 0.27; }; "
      "tilt = \"bright\" { center = -0.5; }; };",
      "index ch0 base60 vel100 | grip_force:value cutoff @1 | "
      "grip_force:value drive @1 | tilt:pitch octave 1 center0.27 @1 | "
      "tilt:value bright center-0.5 @1", 0, NULL },
    { "a latched note and a smoothed knob",
      "device index { a = note 0 { latch; }; grip_top = \"pitch\" { "
      "smooth = 0.1; }; };",
      "index ch0 base60 vel100 | a:note 0 latch @1 | "
      "grip_top:value pitch smooth0.1 @1", 0, NULL },
    { "a layer named before it is declared",
      "device index {\n  grip = hold mod;\n  layer mod { base = \"C3\"; };\n};",
      "index ch0 base60 vel100 | grip:hold mod @2"
      " | layer mod ch0 base48 @3 []", 0, NULL },

    { "a posture",
      "device index {\n  layer fist {\n    when { grip_top = 0.7; "
      "grip_low = 0.6; };\n    roll = \"bend\";\n  };\n};",
      "index ch0 base60 vel100 | layer fist ch0 base-1 @2 when "
      "grip_top>=0.7,grip_low>=0.6 [roll:value bend @4]", 0, NULL },
    { "a posture outside a layer",
      "device index {\n  when { grip_top = 0.7; };\n};", NULL, 2, "layer" },
    { "a posture past the axis",
      "device index { layer f { when { tilt = 2; }; }; };", NULL, 1,
      "-1 to 1" },
    { "an unquoted note",
      "device index {\n  base = C4;\n};", NULL, 2, "quote" },
    { "two notes for a base",
      "device index { base = \"C4 D4\"; };", NULL, 1, "one note" },
    { "a channel past 16",
      "device index {\n  channel = 17;\n};", NULL, 2, "1 to 16" },
    { "channel 0",
      "device index { channel = 0; };", NULL, 1, "1 to 16" },
    { "a velocity of 0",
      "device index { velocity = 0; };", NULL, 1, "1 to 127" },
    { "half a semitone",
      "device index { a = note 1.5; };", NULL, 1, "whole number" },
    { "a sign with a space after it",
      "device index { a = octave - 1; };", NULL, 1, "number" },
    { "a doubled sign",
      "device index { a = note --1; };", NULL, 1, "number" },
    { "a number too big to be one",
      "device index { m = \"cutoff\" { max = 1" ZEROS400 "; }; };",
      NULL, 1, "out of range" },
    { "an exp shape through 0",
      "device index { m = \"cutoff\" { min = 0; shape = exp; }; };",
      NULL, 1, "exp" },
    { "a file cut off before its end",
      "device index {\n  a = note 0;\n", NULL, 2, "'}'" },
    { "an octave of 0",
      "device index { a = octave 0; };", NULL, 1, "not 0" },
    { "a layer nobody declared",
      "device index {\n  grip = hold mod;\n};", NULL, 2, "no layer 'mod'" },
    { "a knob and a note on one control",
      "device index {\n  a = \"cutoff\";\n  a = note 2;\n};", NULL, 3,
      "already" },
    { "a center past the axis",
      "device index { tilt = \"bright\" { center = 2; }; };", NULL, 1,
      "-1 to 1" },
    { "a control bound twice",
      "device index {\n  a = note 0;\n  a = note 2;\n};", NULL, 3, "already" },
    { "a modifier inside a layer",
      "device index {\n  layer mod {\n    grip = hold mod;\n  };\n};",
      NULL, 3, "inside a layer" },
    { "a velocity inside a layer",
      "device index {\n  layer a {\n    velocity = 50;\n  };\n};",
      NULL, 3, "inside a layer" },
    { "two layers of one name",
      "device index {\n  layer a { };\n  layer a { };\n};",
      NULL, 3, "already" },
    { "an arg name with a space",
      "device index { middle = \"cut off\"; };", NULL, 1, "chanarg name" },
    { "an action that isn't one",
      "device index { a = strum 3; };", NULL, 1, "strum" },
    { "a pitch axis with no range",
      "device index { pad_y = pitch { step = octave; }; };",
      NULL, 1, "range" },
    { "a pitch step that isn't one",
      "device index { pad_y = pitch { step = fifth; range = 2; }; };",
      NULL, 1, "fifth" },
    { "a shape that isn't one",
      "device index { m = \"cutoff\" { shape = log; }; };", NULL, 1, "log" },
    { "a velocity range upside down",
      "device index {\n  t = velocity {\n    min = 100;\n    max = 20;\n  };\n};",
      NULL, 2, "min" },
    { "a velocity past 127",
      "device index { t = velocity { max = 200; }; };", NULL, 1, "1 to 127" },
    { "a strike that turns off above where it turns on",
      "device index { trigger = strike 0 { at = 0.5; off = 0.6; }; };",
      NULL, 1, "under" },
    { "a strike past the end of its axis",
      "device index { trigger = strike 0 { at = 1.5; }; };", NULL, 1, "0 to 1" },
    { "a latch on a knob",
      "device index { t = \"cutoff\" { latch; }; };", NULL, 1, "latch" },
    { "a smoothing back in time",
      "device index { t = \"cutoff\" { smooth = -1; }; };", NULL, 1,
      "smooth" },
    { "a setting a block doesn't have",
      "device index { a = note 0 { color = 1; }; };", NULL, 1, "color" },
    { "a missing semicolon",
      "device index {\n  a = note 0\n  b = note 2;\n};", NULL, 3, "';'" },
    { "a block with no closing semicolon, then a comment",
      "device index { }\n\n# the end\n", NULL, 1, "';'" },
    { "a profile with two devices",
      "device index { };\ndevice index { };", NULL, 2, "one device" },
    { "a profile with no device",
      "layer a { };", NULL, 1, "device" },
};

int
main (void)
{
    for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++)
    {
        const Row &r = ROWS[i];
        thcHidBlock blk;
        string err;
        int errLine = -1;
        bool ok = thcHidParseProfile(r.text, blk, err, errLine);
        string why;

        if (r.want && !ok)
            why = "refused: " + std::to_string(errLine) + ": " + err;
        else if (r.want && describe(blk) != r.want)
            why = "read as\n    " + describe(blk) + "\n  wanted\n    " + r.want;
        else if (!r.want && ok)
            why = "accepted as " + describe(blk);
        else if (!r.want && (errLine != r.errLine ||
                             err.find(r.errHas) == string::npos))
            why = "refused at " + std::to_string(errLine) + " with \"" + err +
                  "\"; wanted line " + std::to_string(r.errLine) +
                  " and \"" + r.errHas + "\"";

        if (why.empty())
            printf("ok    %s\n", r.name);
        else
        {
            printf("FAIL  %s\n  %s\n", r.name, why.c_str());
            failed++;
        }
    }

    /* A block read into twice holds the second profile, not both. */
    thcHidBlock twice;
    string err;
    int errLine;

    thcHidParseProfile("device index { a = note 1; };", twice, err, errLine);
    if (!thcHidParseProfile("device index { a = note 2; };", twice, err,
                            errLine) || describe(twice) !=
        "index ch0 base60 vel100 | a:note 2 @1")
    {
        printf("FAIL  a block read into twice\n  read as %s\n",
               describe(twice).c_str());
        failed++;
    }
    else
        printf("ok    a block read into twice\n");

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
