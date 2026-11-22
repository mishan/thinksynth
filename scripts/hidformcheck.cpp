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
 * hidformcheck -- the configurator's form over the phone's profile
 * (hid/thhForm.h).
 *
 * The form shows the phone's own bindings until the player has theirs, an
 * ignore as off, and a layer's bindings beside; an edit of its own writes
 * them all as the player's, the last taken out writes an ignore, and one
 * added to a control that is off drops the ignore; and an edit that
 * wouldn't play is refused with the text as it was. On the Index, a hand's
 * block is shown beside, and what the patch's controller block binds is
 * marked as the patch's.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "thhForm.h"
#include "thhIndex.h"
#include "thhPhone.h"
#include "thhProfileEdit.h"

using std::string;

static int failed = 0;

static void
check (bool good, const string &what, const string &detail = "")
{
    if (good)
        printf("ok    %s\n", what.c_str());
    else
    {
        printf("FAIL  %s\n%s%s", what.c_str(), detail.c_str(),
               detail.empty() ? "" : "\n");
        failed++;
    }
}

static const thhFormControl &
of (const thhForm &f, const string &control)
{
    for (const thhFormControl &c : f.controls)
        if (c.control == control)
            return c;
    static const thhFormControl none = {};
    return none;
}

static string
texts (const thhFormControl &c)
{
    string out;

    for (const thHidBinding &b : c.bindings)
        out += thhBindingText(b) + " ";
    for (const string &l : c.elsewhere)
        out += "[" + l + "] ";
    return out + (c.yours ? "yours" : "its own") + (c.off ? ", off" : "");
}

static void
checkForm ()
{
    const thhForm plain = thhFormOf(thhPhoneControls(), THH_PHONE_PROFILE, "");
    const thhForm mine = thhFormOf(thhPhoneControls(), THH_PHONE_PROFILE,
        "device phone {\n"
        "    touch_1 = note 7;\n"
        "    ignore touch_2;\n"
        "    touch_4 = hold up;\n"
        "    layer up { touch_1 = note 12; };\n"
        "};\n");
    const thhForm broken = thhFormOf(thhPhoneControls(), THH_PHONE_PROFILE,
                                     "device phone { touch_1 = ; };");
    const thhForm both = thhFormOf(thhPhoneControls(), THH_PHONE_PROFILE,
        "device phone { ignore touch_1; touch_1 = note 5; };");
    const thhForm other = thhFormOf(thhPhoneControls(), THH_PHONE_PROFILE,
                                    "device index { a = note 5; };");

    check(plain.controls.size() == 17 && plain.controls[0].control == "touch_1" &&
          plain.controls[5].control == "tilt_x" && plain.controls[5].axis,
          "a row for every control, the buttons first");

    const struct { const thhForm &form; const char *control, *want, *what; } rows[] = {
        { plain, "touch_1", "touch_1 = note 0; its own",
          "the phone's own binding, as its own" },
        { plain, "touch_5", "its own", "a control nothing binds, with none" },
        { mine, "touch_1", "touch_1 = note 7; [layer up: touch_1 = note 12;] yours",
          "the player's over it, and a layer's beside" },
        { mine, "touch_2", "its own, off", "an ignore, as off with nothing" },
        { both, "touch_1", "touch_1 = note 5; yours",
          "an ignore under the player's own binding, as theirs, not off" },
    };

    for (const auto &r : rows)
        check(texts(of(r.form, r.control)) == r.want, r.what,
              texts(of(r.form, r.control)));
    check(mine.layers == std::vector<string>{ "up" }, "the player's layers");
    check(!broken.problem.empty() && texts(of(broken, "touch_1")) ==
          "touch_1 = note 0; its own", "text that doesn't read is said, its own shown",
          broken.problem);
    check(!other.problem.empty() && texts(of(other, "touch_1")) ==
          "touch_1 = note 0; its own", "and another device's", other.problem);

    const thhForm index = thhFormOf(thhIndexControls(), THH_INDEX_PROFILE, "",
                                    "device index { a = note 3; };");

    check(texts(of(index, "trigger")) == "trigger = strike 0 { at = 0.5; }; "
          "[left hand: trigger = strike -12 { at = 0.5; };] its own" &&
          texts(of(index, "turn")) == "turn = \"pan\" { min = -1; max = 1; smooth = 0.05; }; "
          "[layer fist: turn = bend { range = 2; smooth = 0.05; };] "
          "[left hand: turn = bend { range = 2; smooth = 0.05; of = both; };] its own" &&
          texts(of(index, "grip_mid")).find("[right hand: ignore grip_mid;]") != string::npos,
          "a hand's block, and its layers, beside",
          texts(of(index, "trigger")) + "\n" + texts(of(index, "turn")) + "\n" +
          texts(of(index, "grip_mid")));
    check(of(index, "a").patched && !of(index, "b").patched,
          "what the patch's block binds, as the patch's");

    const thhForm rebound = thhFormOf(thhIndexControls(), THH_INDEX_PROFILE,
                                      "device index { trigger = strike 2; };");

    check(texts(of(rebound, "trigger")) == "trigger = strike 2; [left hand: trigger = "
          "strike -12 { at = 0.5; }; (yours plays instead)] yours",
          "a hand's own the player's replaces, said so", texts(of(rebound, "trigger")));
}

static void
checkEdits ()
{
    const string some =
        "device phone {\n"
        "    touch_1_x = \"cutoff\";  # mine\n"
        "    touch_1_x = aux 2;\n"
        "};\n";
    const string off = "device phone {\n    ignore touch_2;\n};\n";
    const struct
    {
        const char *what;
        string text, op, control;
        int which;
        string fields, want;
    } rows[] = {
        { "a change to its own writes it as yours", "", "set", "touch_1", 0,
          "semis=7", "device phone {\n    touch_1 = note 7;\n};\n" },
        { "with the options it had", "", "set", "touch_1_x", 0, "smooth=0.1",
          "device phone {\n    touch_1_x = bend { scale = pentatonic; "
          "range = 15; smooth = 0.1; };\n};\n" },
        { "a kind starts the binding afresh", "", "set", "touch_1", 0,
          "kind=octave", "device phone {\n    touch_1 = octave 1;\n};\n" },
        { "with none of the old kind's options", "", "set", "touch_1_x", 0,
          "kind=aux", "device phone {\n    touch_1_x = aux 0;\n};\n" },
        { "one added to its own keeps its own", "", "add", "tilt_x", -1,
          "kind=value\narg=cutoff\nmin=\nmax=2",
          "device phone {\n    tilt_x = aux 1 { smooth = 0.05; };\n"
          "    tilt_x = \"cutoff\" { max = 2; };\n};\n" },
        { "the last of its own taken out is off", "", "remove", "touch_2", 0,
          "", "device phone {\n    ignore touch_2;\n};\n" },
        { "one of yours taken out is that one, its comment kept", some, "remove",
          "touch_1_x", 0, "", "device phone {\n    # mine\n    touch_1_x = aux 2;\n};\n" },
        { "one of yours changed is that one", some, "set", "touch_1_x", 1,
          "semis=3", "device phone {\n    touch_1_x = \"cutoff\";  # mine\n"
          "    touch_1_x = aux 3;\n};\n" },
        { "reset drops yours", some, "reset", "touch_1_x", -1, "",
          "device phone {\n    # mine\n};\n" },
        { "off drops its own", "", "off", "touch_1", -1, "",
          "device phone {\n    ignore touch_1;\n};\n" },
        { "one added while off is on again", off, "add", "touch_2", -1,
          "kind=note\nsemis=3", "device phone {\n    touch_2 = note 3;\n};\n" },
        { "the player's last taken out is off, not its own back",
          "device phone {\n    touch_1 = note 5;\n};\n", "remove", "touch_1", 0, "",
          "device phone {\n    ignore touch_1;\n};\n" },
        { "off where the phone binds nothing drops the player's",
          "device phone {\n    touch_5 = note 5;\n};\n", "off", "touch_5", -1, "",
          "device phone {\n};\n" },
        { "a new strike's at takes its off along", "", "add", "tilt_y", -1,
          "kind=strike\nat=0.3", "device phone {\n    tilt_y = strike 0 { at = 0.3; };\n};\n" },
        { "and an off the player gave stays", "device phone {\n    tilt_y = strike 0 "
          "{ at = 0.8; off = 0.1; };\n};\n", "set", "tilt_y", 0, "at=0.9",
          "device phone {\n    tilt_y = strike 0 { at = 0.9; off = 0.1; };\n};\n" },
    };

    for (const auto &r : rows)
    {
        string t = r.text, err;
        const bool ok = thhFormEdit(t, thhPhoneControls(), THH_PHONE_PROFILE,
                                    r.op, r.control, r.which, r.fields, err);

        check(ok && t == r.want, r.what, "got:\n" + t + err);
    }

    const struct { const char *what; string text, op, control; int which; string fields; }
        refused[] = {
        { "an action the control can't take", "", "set", "touch_1", 0, "kind=bend" },
        { "a second note on a button", "", "add", "touch_1", -1, "kind=note" },
        { "a number that isn't one", "", "set", "touch_1", 0, "semis=x" },
        { "a whole number that isn't whole", "", "set", "touch_1", 0, "semis=1.5" },
        { "a number that isn't finite", "", "set", "touch_1", 0, "semis=nan" },
        { "a scale there isn't", "", "set", "touch_1_x", 0, "scale=nope" },
        { "a layer there isn't", "", "set", "touch_1", 0, "kind=hold" },
        { "a binding there isn't", "", "set", "touch_5", 0, "semis=1" },
        { "a control there isn't", "", "off", "touch_9", -1, "" },
        { "text that doesn't read", "device phone { touch_1 = ; };", "off",
          "touch_1", -1, "" },
    };

    for (const auto &r : refused)
    {
        string t = r.text, err;
        const bool ok = thhFormEdit(t, thhPhoneControls(), THH_PHONE_PROFILE,
                                    r.op, r.control, r.which, r.fields, err);

        check(!ok && t == r.text && !err.empty(), string("refused: ") + r.what,
              "got:\n" + t + err);
    }
}

int
main (void)
{
    checkForm();
    checkEdits();

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed;
}
