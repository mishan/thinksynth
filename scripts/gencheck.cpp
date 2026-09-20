/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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
 */

/* gencheck -- the composer framework's gate.
 *
 * Three claims are held down here, because each is the kind that decays
 * silently if nothing is watching:
 *
 * 1. The shared pitch parser is right, at the values people argue about
 *    (middle C, the octave boundary, flats on C).
 *
 * 2. The loader rejects what docs/GEN_FORMAT.md says it rejects, with the
 *    file and line in the message. Each bad file is generated here --
 *    the corpus cannot contain them, for the same reason argtype builds
 *    its own .dsp files.
 *
 * 3. Replay determinism: the same .gen with its pinned seed, rendered
 *    twice through the virtual clock with a reset between, delivers a
 *    byte-identical event stream. This is the framework's foundational
 *    promise ("same file + same seed = same piece") and the reason
 *    every composer draws randomness from its instance seed.
 *
 * Headless on purpose: no display, no audio device, no Glib main loop.
 * The scheduler's stepTransport is the virtual clock; sigDelivered is
 * the tape.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <glibmm.h>

#include "think.h"

#include "libthink/thDynLib.h"
#include "libthink/thMidiChan.h"
#include "libthink/thSynthCommand.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcGenEdit.h"
#include "thcNodeHost.h"
#include "GenCatalog.h"

static int failures = 0;

static void
fail (const std::string &what)
{
    fprintf(stderr, "gencheck: FAIL: %s\n", what.c_str());
    failures++;
}

/* The synth every check shares, and the one thing this harness has to do
 * with it besides hand it to a scheduler: empty its command queue.
 *
 * Every delivered note posts a command for the audio thread, and nothing
 * here is an audio thread -- so the ring filled after the first few
 * thousand events and stayed full, printing "command queue full" for the
 * rest of the run and dropping everything queued after it.
 *
 * That was noise while the only thing in the ring was notes nobody was
 * listening to. It stopped being noise when a piece began carrying its
 * own instruments: loading one queues a SET_CHANNEL, and a SET_CHANNEL
 * dropped on the floor is loadTree returning NULL -- an instrument that
 * fails to load because the *previous* check played too many notes,
 * which is a beautifully confusing way to fail.
 *
 * Once per render and once before a load that might carry an instrument,
 * not once per step. drainCommands empties the whole ring in one call,
 * so a single window leaves it clean for whatever comes next. A window
 * per step was tried first and is what a real audio thread does; it also
 * turned a 0.07-second gate into a 28-second one, which is a fine way to
 * teach people to stop running it. */
static thSynth *tapeSynth = NULL;

/* And the one that never renders (thSynth::setSilent), which checkSilent
 * holds up against it. Stepped wherever the rendering one is, because
 * that is what a mirror does with it: a silent synth still queues a
 * SET_CHANNEL per instrument, and a ring nobody drains is the failure
 * the mode exists to remove, not one the gate should reproduce. */
static thSynth *silentSynth = NULL;

static void
drainSynth (void)
{
    if (tapeSynth != NULL)
        tapeSynth->process();

    if (silentSynth != NULL)
        silentSynth->process();
}

/* ---- 1. the pitch parser ---------------------------------------------- */

static void
checkNote (const char *text, int expect)
{
    std::vector<int> out;
    std::string bad;

    if (!thcGenLoader::parseNoteList(text, out, bad) || out.size() != 1)
    {
        fail(std::string("parseNoteList refused '") + text + "'");
        return;
    }

    if (out[0] != expect)
    {
        std::ostringstream s;

        s << "'" << text << "' resolved to " << out[0]
          << ", wanted " << expect;
        fail(s.str());
    }
}

static void
checkNotes (void)
{
    checkNote("C4", 60);        /* middle C, the convention the spec pins */
    checkNote("A4", 69);
    checkNote("C0", 12);
    checkNote("A0", 21);        /* bottom of the piano                    */
    checkNote("G9", 127);       /* top of MIDI                            */
    checkNote("C#4", 61);
    checkNote("Db4", 61);       /* enharmonic agreement                   */
    checkNote("Cb4", 59);       /* a flat can cross the octave boundary   */
    checkNote("B#3", 60);       /* and so can a sharp                     */

    std::vector<int> out;
    std::string bad;

    if (!thcGenLoader::parseNoteList("F3 Ab3 C4", out, bad) ||
        out.size() != 3 || out[0] != 53 || out[1] != 56 || out[2] != 60)
        fail("space-separated list did not resolve to 53,56,60");

    if (!thcGenLoader::parseNoteList("F3,Ab3,C4", out, bad) ||
        out.size() != 3)
        fail("comma-separated list did not resolve");

    if (thcGenLoader::parseNoteList("H3", out, bad))
        fail("'H3' was accepted; H is not a note");

    if (thcGenLoader::parseNoteList("C", out, bad))
        fail("'C' with no octave was accepted");

    if (thcGenLoader::parseNoteList("", out, bad))
        fail("an empty note list was accepted");

    /* A rest is an entry, resolved as -1, that takes its turn in a pool
       and is not a pitch anywhere else. */
    if (!thcGenLoader::parseNoteList("C4 . E4", out, bad) ||
        out.size() != 3 || out[0] != 60 || out[1] != -1 || out[2] != 64)
        fail("'C4 . E4' did not resolve to 60,-1,64");
}

/* ---- module loading --------------------------------------------------- */

static void
loadComposers (const std::string &pluginDir,
               std::map<std::string, thcPlugin *> &out)
{
    std::filesystem::path root =
        std::filesystem::path(pluginDir) / "composer";
    std::error_code ec;

    if (!std::filesystem::is_directory(root, ec))
        return;

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        out[p->name()] = p;

        /* Pin the module's mapping -- and, more to the point, its
           dependency closure -- for the life of the process: a second
           dlopen of the same path bumps the loader's reference count,
           and this handle is never closed on purpose.

           Why: on runners where cairo links gobject (cairo >= 1.18 as
           Ubuntu 24.04 ships it), a draw module's dlopen is what first
           loads the glib stack, and the teardown's dlclose -- which
           this harness performs deliberately, so the unload path runs
           under ASan at all -- would drop the last reference. The
           loader then unmaps those libraries and their once-per-process
           init heap, which glib documents as never-freed, turns into
           six LeakSanitizer reports. And they cannot be suppressed by
           library name, because an unmapped library symbolizes as
           "<unknown module>": the name a suppression would match is
           exactly what the unload destroyed.

           So: keep the mapping. dlclose still runs in ~thcPlugin and
           still exercises its path; the reference held here just means
           the count never reaches zero, the libraries stay mapped, and
           their init-once allocations remain reachable at exit --
           which is what they are in every process that links them the
           ordinary way. */
        thDynLib::open(f.path().string());
    }
}

/* ---- 2. loader validation --------------------------------------------- */

/* Write `body' to a scratch .gen, load it, and demand it fails with a
 * message mentioning `expect'. The message contract matters as much as
 * the rejection: "by name and line" is what makes an error actionable. */
static void
expectReject (const std::map<std::string, thcPlugin *> &plugins,
              thSynth *synth, const char *label, const std::string &body,
              const std::string &expect)
{
    /* thUtil::tempFile, not a fixed name under the shared temp dir:
       two gencheck processes (parallel ctest, two build trees) writing
       the same scratch path is a flaky failure nobody can reproduce. */
    std::string path = thUtil::tempFile(
        std::string("gencheck-") + label + "-");

    if (path.empty())
    {
        fail(std::string(label) + ": could not make a scratch file");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;

        if (!out.good())
        {
            fail(std::string(label) + ": could not write " + path);
            std::filesystem::remove(path);
            return;
        }
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    /* A file that declares an instrument queues a SET_CHANNEL, and a
       full command ring would drop it -- so a rejection case would
       "fail" on the wrong error. See drainSynth. */
    drainSynth();

    if (loader.load(path, &sched))
        fail(std::string(label) + ": a file that should not load, loaded");
    else
    {
        bool found = false;

        for (size_t i = 0; i < loader.errors().size(); i++)
            if (loader.errors()[i].find(expect) != std::string::npos)
                found = true;

        if (!found)
        {
            std::string got = loader.errors().empty()
                ? "(no errors recorded)" : loader.errors()[0];

            fail(std::string(label) + ": rejected, but the message was '" +
                 got + "' with no mention of '" + expect + "'");
        }

        if (sched.chainCount() != 0)
            fail(std::string(label) +
                 ": a failed load left chains in the scheduler");
    }

    std::filesystem::remove(path);
}

static void
checkValidation (const std::map<std::string, thcPlugin *> &plugins,
                 thSynth *synth)
{
    /* A duration with no unit: a value carries its unit or it is a load
       error, which is the whole point of the format's time rules. */
    expectReject(plugins, synth, "bare-duration",
        "chain c { stage s gen::eno_line { period = 20; };"
        " sink { channel = 1; }; };",
        "write a unit");

    /* A plugin that does not exist, by name. */
    expectReject(plugins, synth, "no-such-plugin",
        "chain c { stage s gen::no_such_thing { };"
        " sink { channel = 1; }; };",
        "no_such_thing");

    /* A param the plugin never registered, by name. */
    expectReject(plugins, synth, "no-such-param",
        "chain c { stage s gen::eno_line { frobnicate = 3; };"
        " sink { channel = 1; }; };",
        "frobnicate");

    /* gen:: asked of a transformer. */
    expectReject(plugins, synth, "wrong-role",
        "chain c { stage s gen::quantize { };"
        " sink { channel = 1; }; };",
        "cannot be a gen:: stage");

    /* A knob used before it is declared. */
    expectReject(plugins, synth, "undeclared-knob",
        "chain c { stage s gen::eno_line { prob = @nope; };"
        " sink { channel = 1; }; };",
        "@nope");

    /* A chain with no sink has nowhere to deliver. */
    expectReject(plugins, synth, "no-sink",
        "chain c { stage s gen::eno_line { }; };",
        "has no sink");

    /* Textual order is execution order; a stage after a sink is a
       contradiction, not a style choice. */
    expectReject(plugins, synth, "stage-after-sink",
        "chain c { stage s gen::eno_line { }; sink { channel = 1; };"
        " stage t xform::quantize { }; };",
        "stage after sink");

    /* All transformers and no input: nothing would ever flow. */
    expectReject(plugins, synth, "no-source",
        "chain c { stage s xform::quantize { };"
        " sink { channel = 1; }; };",
        "no generator");

    /* A scale nobody declared, by name. */
    expectReject(plugins, synth, "no-such-scale",
        "chain c { stage s gen::eno_line { notes = ghost; };"
        " sink { channel = 1; }; };",
        "ghost");

    /* A seed after a chain cannot mean what it says. */
    expectReject(plugins, synth, "late-seed",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; }; };\n"
        "seed 42;",
        "before the first chain");
}

/* ---- 3. replay determinism -------------------------------------------- */

/* Render `seconds' of the piece through the virtual clock and tape every
 * delivered event, at full precision -- %.17g is round-trip-exact for a
 * double, so two identical streams compare identical and two different
 * ones cannot collide. */
static std::string
render (thcScheduler &sched, double seconds, double step)
{
    std::string tape;

    /* Every delivered note posts a command for an audio thread that is not
       there. Counting them is how the drain below knows when to run: a
       cadence in *steps* cannot work, because a grammar emits a whole
       phrase in one step and a phrase can be longer than the ring. */
    long posted = 0;

    sigc::connection conn = sched.sigDelivered.connect(
        [&tape, &posted](const thcEvent &ev)
        {
            char buf[160];

            if (ev.type == THC_EV_NOTE)
                snprintf(buf, sizeof(buf), "N %.17g %d %d %d %.17g %.9g\n",
                         ev.at, ev.channel, ev.u.note.note,
                         ev.u.note.velocity, ev.u.note.duration,
                         (double)ev.u.note.level);
            /* Structure edits are on the tape for the same reason notes
               are: they are what the piece did. A replay gate that
               diffed only the notes would call a piece identical while
               it rebuilt its instrument at different times. */
            else if (ev.type == THC_EV_PATCH)
                snprintf(buf, sizeof(buf), "P %.17g %d %s\n",
                         ev.at, ev.channel,
                         ev.u.patch.name ? ev.u.patch.name : "");
            else if (ev.type == THC_EV_NODEARG)
                snprintf(buf, sizeof(buf), "E %.17g %d %s %s %.17g\n",
                         ev.at, ev.channel,
                         ev.u.nodearg.node ? ev.u.nodearg.node : "",
                         ev.u.nodearg.arg ? ev.u.nodearg.arg : "",
                         (double)ev.u.nodearg.value);
            else if (ev.type == THC_EV_CHANARG)
                snprintf(buf, sizeof(buf), "C %.17g %d %s %.17g\n",
                         ev.at, ev.channel,
                         ev.u.chanarg.name ? ev.u.chanarg.name : "",
                         (double)ev.u.chanarg.value);
            /* Named, not fallen through to. The chanarg case used to be
               the `else', which read u.chanarg.name out of whatever
               arrived -- a THC_EV_NOTEOFF landing there hands printf two
               ints as a char *. Unreachable in a headless run with no
               MIDI in it, and one live-input piece away from not
               being. */
            else
                snprintf(buf, sizeof(buf), "? %.17g %d %d\n",
                         ev.at, ev.channel, (int)ev.type);

            tape += buf;
            posted++;
        });

    sched.start();

    /* Every so often, not every step. The ring holds
       TH_COMMAND_QUEUE_SIZE commands and a minute of a busy piece posts
       tens of thousands, so a render that only drained at the end spent
       most of itself full, printing "command queue full" for every note
       after the first thousand -- 35_000 lines of stderr with this gate's
       nineteen real diagnostics somewhere inside them. Draining on a
       cadence well under the ring's depth keeps it from ever filling; a
       window per step is what a real audio thread does and is also what
       turned a 0.07-second gate into a 28-second one. */
    /* `sched.running()' as well as the clock: a piece that ends itself
       -- `section end;' -- stops the transport, and stepTransport on a
       stopped one does nothing, so the clock would never reach
       `seconds'. */
    while (sched.now() < seconds && sched.running())
    {
        sched.stepTransport(step);

        if (posted >= TH_COMMAND_QUEUE_SIZE / 4)
        {
            drainSynth();
            posted = 0;
        }
    }

    sched.stop();
    conn.disconnect();

    /* After stop(), so the note-offs it flushes are in the ring this
       empties too. */
    drainSynth();

    return tape;
}

/* Where two streams parted, because "different" alone is undebuggable. */
static void
showDivergence (const std::string &a, const std::string &b,
                const char *labelA, const char *labelB)
{
    size_t n = 0;

    while (n < a.size() && n < b.size() && a[n] == b[n])
        n++;

    size_t line0 = a.rfind('\n', n);

    line0 = line0 == std::string::npos ? 0 : line0 + 1;

    fprintf(stderr, "  %s: %.60s\n", labelA, a.c_str() + line0);
    fprintf(stderr, "  %s: %.60s\n", labelB, b.c_str() + line0);
}

/* A rewind replays the load -- and the edits made since.
 *
 * The desktop's editor pokes the live store so a change to the work file
 * is audible without a reload (ComposerWindow::applyParam): `prob = 0.5'
 * typed while the piece is playing sets the param there and then. What a
 * rewind owes that is the file as it now stands, because that is what a
 * reload would read. Recording only what happened before the first
 * start, and replaying only that, put the loaded value back and left the
 * editor, the panel and the work file all saying something the piece was
 * not playing -- and dropped a binding made after Play while leaving the
 * knob's signal connected to it.
 *
 * So the store keeps two lists and replays both, in order. This is the
 * gate on the second one.
 */
/* ---- arithmetic over signals (docs/GEN_FORMAT.md 5a) ------------------------
 *
 * `prob = lfo->out * 0.5 + 0.5' is sugar for the math::mul and math::add a
 * chain used to have to spell out three lines at a time. The claim is an
 * equivalence, so what is checked is one: the two spellings must deliver the
 * same tape, event for event.
 */

/* Loads `body' and renders `seconds' of it. Empty on a load failure, which
   the caller reports against its own label. */
static std::string
renderBody (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth, const char *label, const std::string &body,
            double seconds)
{
    std::string path = thUtil::tempFile(
        std::string("gencheck-") + label + "-");

    if (path.empty())
    {
        fail(std::string(label) + ": could not make a scratch file");
        return "";
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    drainSynth();

    std::string tape;

    if (!loader.load(path, &sched))
    {
        std::string why = loader.errors().empty() ? "(no errors recorded)"
                                                  : loader.errors()[0];

        fail(std::string(label) + ": did not load -- " + why);
    }
    else
        tape = render(sched, seconds, 0.02);

    std::filesystem::remove(path);

    return tape;
}

static void
checkExpressions (const std::map<std::string, thcPlugin *> &plugins,
                  thSynth *synth)
{
    const std::string head =
        "seed 7;\n"
        "@tide = 0.02;\n";

    /* A stage param. ebb.gen's crossfade, both ways round. */
    {
        const std::string nodes = head +
            "chain c {\n"
            "    stage lfo  osc::simple { freq = @tide; waveform = 0; "
                "amp = 1; };\n"
            "    stage half math::mul   { in0 = lfo->out;  in1 = 0.5; };\n"
            "    stage mid  math::add   { in0 = half->out; in1 = 0.5; };\n"
            "    stage src gen::eno_line { notes = \"A1 A2 E2\"; "
                "period = 2 s; jitter = 0 s; prob = mid->out; hold = 1 s; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        const std::string sugar = head +
            "chain c {\n"
            "    stage lfo  osc::simple { freq = @tide; waveform = 0; "
                "amp = 1; };\n"
            "    stage src gen::eno_line { notes = \"A1 A2 E2\"; "
                "period = 2 s; jitter = 0 s; "
                "prob = lfo->out * 0.5 + 0.5; hold = 1 s; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        const std::string a = renderBody(plugins, synth, "expr-nodes", nodes,
                                         30.0);
        const std::string b = renderBody(plugins, synth, "expr-sugar", sugar,
                                         30.0);

        if (a.empty())
            ;   /* renderBody already said why */
        else if (a != b)
        {
            fail("an expression on a stage param does not compose as the "
                 "nodes it replaces");
            showDivergence(a, b, "nodes", "expression");
        }
    }

    /* A node arg, and a chain whose only nodes are the ones the arithmetic
       made -- the host has to be created on demand for that to work at all.
       round.gen's `twice', both ways round. */
    {
        const std::string nodes = head +
            "@pace = 0.25;\n"
            "chain c {\n"
            "    stage twice math::mul { in0 = @pace; in1 = 2; };\n"
            "    stage src gen::lsystem { axiom = \"X\"; "
                "rules = \"X=F+FX\"; depth = 3; notes = \"C3 E3 G3\"; "
                "step = twice->out; hold = 0.5 s; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        const std::string sugar = head +
            "@pace = 0.25;\n"
            "chain c {\n"
            "    stage src gen::lsystem { axiom = \"X\"; "
                "rules = \"X=F+FX\"; depth = 3; notes = \"C3 E3 G3\"; "
                "step = @pace * 2; hold = 0.5 s; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        const std::string a = renderBody(plugins, synth, "expr-knob-nodes",
                                         nodes, 20.0);
        const std::string b = renderBody(plugins, synth, "expr-knob-sugar",
                                         sugar, 20.0);

        if (a.empty())
            ;
        else if (a != b)
        {
            fail("an expression over a knob does not compose as the node it "
                 "replaces");
            showDivergence(a, b, "nodes", "expression");
        }
    }

    /* A function, which has no spelling without the sugar at all. */
    {
        const std::string sugar = head +
            "chain c {\n"
            "    stage lfo osc::simple { freq = @tide; waveform = 0; "
                "amp = 1; };\n"
            "    stage src gen::eno_line { notes = \"A1\"; period = 2 s; "
                "jitter = 0 s; prob = clamp(abs(lfo->out), 0.2, 0.8); "
                "hold = 1 s; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        renderBody(plugins, synth, "expr-call", sugar, 10.0);
    }

    /* And the refusals. */
    expectReject(plugins, synth, "expr-noteset",
        "chain c { stage s gen::eno_line { notes = \"A1\" + 2; "
        "period = 2 s; }; sink { channel = 1; }; };",
        "not numeric");

    expectReject(plugins, synth, "expr-duration",
        "chain c { stage s gen::eno_line { notes = \"A1\"; "
        "period = 2 * 3; }; sink { channel = 1; }; };",
        "write a unit");

    expectReject(plugins, synth, "expr-unknown-knob",
        "chain c { stage s gen::eno_line { notes = \"A1\"; period = 2 s; "
        "prob = @nosuch * 2; }; sink { channel = 1; }; };",
        "not a declared knob");

    expectReject(plugins, synth, "expr-no-such-function",
        "chain c { stage s gen::eno_line { notes = \"A1\"; period = 2 s; "
        "prob = wobble(0.5) * 2; }; sink { channel = 1; }; };",
        "is not a function");

    /* And the writer leaves one alone. ebb.gen carries the first shipped
       expression on a param; setParam over it must refuse rather than
       splice a number across the author's arithmetic. */
    {
        std::string path = thUtil::tempFile("gencheck-expr-write-");

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << head
                << "chain c {\n"
                   "    stage lfo osc::simple { freq = @tide; };\n"
                   "    stage src gen::eno_line { notes = \"A1\"; "
                   "period = 2 s; prob = lfo->out * 0.5 + 0.5; };\n"
                   "    sink { channel = 1; };\n"
                   "};\n";
        }

        std::string why;

        if (thcGenEdit::setParam(path, "c", 1, "prob", "0.5", why) ==
                thcGenEdit::OK)
            fail("setParam wrote over an expression");

        std::string after;

        /* In its own scope: thcGenEdit writes a temporary and renames it
           over the target, and Windows refuses a rename onto a file
           something still has open. The edit below is what would fail. */
        {
            std::ifstream in(path.c_str());

            after.assign((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        }

        if (after.find("lfo->out * 0.5 + 0.5") == std::string::npos)
            fail("a refused setParam changed the expression anyway");

        /* But a param beside it is still editable -- only the expression is
           off limits. */
        if (thcGenEdit::setParam(path, "c", 1, "period", "3 s", why) !=
                thcGenEdit::OK)
            fail(std::string("setParam refused a param beside an "
                             "expression: ") + why);

        std::filesystem::remove(path);
    }

    expectReject(plugins, synth, "expr-arity",
        "chain c { stage s gen::eno_line { notes = \"A1\"; period = 2 s; "
        "prob = exp2(0.5, 2) * 2; }; sink { channel = 1; }; };",
        "takes 1 argument");

    /* ---- and it groups the way a .dsp groups it ------------------------ */

    /* The claim docs/GEN_FORMAT.md 5a makes: one language, whichever file it is
     * written in. Two parsers say it -- thinklang.yy's rules and the three
     * hand-written functions above -- so the way to hold them together is to
     * ask both the same questions and compare the answers.
     *
     * exprcheck folds the same list against the .dsp grammar. Here each is a
     * `vel', which is an integer a tape reports, so a disagreement shows up
     * as a velocity rather than as a silence.
     *
     * `-60 + 100' is the row that used to differ: this parser binds a sign
     * to its operand and the .dsp grammar scoped it over everything to the
     * right, so the same text was 40 here and -160 there. `2 + 3 * 4' pins
     * the precedence and `1 - 2 + 3' the right-associativity -- a `-' takes
     * the additions after it too.
     */
    {
        static const struct { const char *rhs; int want; } cases[] = {
            { "20 + 2 * 10",    40 },
            { "60 - 10 - 5",    55 },   /* 60 - (10 - 5) */
            { "100 - 20 + 40",  40 },   /* 100 - (20 + 40) */
            { "-60 + 100",      40 },   /* (-60) + 100, not -(60 + 100) */
            { "20 * -1 + 60",   40 },
            { "clamp(10, 40, 90)", 40 },
            { "exp2(2) * 10",   40 },
        };

        bool bad = false;

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]) && !bad; i++)
        {
            char body[512], want[512];

            snprintf(body, sizeof(body),
                     "seed 7;\nchain c {\n"
                     "    stage src gen::eno_line { notes = \"A3\"; "
                     "period = 1 s; jitter = 0 s; prob = 1; hold = 0.2 s; "
                     "vel = %s; };\n    sink { channel = 1; };\n};\n",
                     cases[i].rhs);

            snprintf(want, sizeof(want),
                     "seed 7;\nchain c {\n"
                     "    stage src gen::eno_line { notes = \"A3\"; "
                     "period = 1 s; jitter = 0 s; prob = 1; hold = 0.2 s; "
                     "vel = %d; };\n    sink { channel = 1; };\n};\n",
                     cases[i].want);

            const std::string a =
                renderBody(plugins, synth, "expr-group", body, 4.0);
            const std::string b =
                renderBody(plugins, synth, "expr-group-ref", want, 4.0);

            if (a.empty() || b.empty())
                bad = true;
            else if (a != b)
            {
                fail(std::string("`") + cases[i].rhs + "' folds as a .dsp "
                     "folds it");
                showDivergence(a, b, cases[i].rhs, "the folded value");
                bad = true;
            }
        }
    }

    /* ---- and removing a knob one reads leaves a file that loads --------- */

    /* removeKnob rewrites every `@name' to the value the params were
     * hearing, precisely so that deleting a knob cannot leave a dangling
     * reference behind. It compared the whole value, which was every binding
     * there was until a param could be arithmetic: `step = @pace * 2' is not
     * `@pace', so the declaration went and the reference stayed, and
     * round.gen stopped loading while the edit reported success.
     */
    {
        std::string path = thUtil::tempFile("gencheck-expr-knob-");

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << "seed 7;\n@depth = 0.3;\n@warmth = 0.7;\n"
                   "chain c {\n"
                   "    stage src gen::eno_line { notes = \"A1\"; "
                   "period = 2 s; prob = @depth * 2; vel = @warmth; };\n"
                   "    sink { channel = 1; };\n};\n";
        }

        std::string why;
        int rewritten = 0;

        if (thcGenEdit::removeKnob(path, "depth", 0.25, rewritten, why) !=
                thcGenEdit::OK)
            fail(std::string("removeKnob over an expression: ") + why);
        else
        {
            std::string after;

            {
                std::ifstream in(path.c_str());

                after.assign((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            }

            if (after.find("prob = 0.25 * 2") == std::string::npos)
                fail("removeKnob puts the value inside the arithmetic");
            else if (after.find("@depth") != std::string::npos)
                fail("removeKnob leaves no reference to the knob behind");
            else if (after.find("vel = @warmth") == std::string::npos)
                fail("removeKnob leaves the other knob alone");
            else
            {
                thcScheduler sched(synth);
                thcGenLoader loader(plugins);

                drainSynth();

                if (!loader.load(path, &sched))
                    fail("the file no longer loads after its knob was "
                         "removed: " +
                         (loader.errors().empty() ? std::string("(no errors "
                          "recorded)") : loader.errors()[0]));
            }
        }

        std::filesystem::remove(path);
    }
}

static void
checkLiveEdits (const std::map<std::string, thcPlugin *> &plugins,
                thSynth *synth, const std::string &genFile)
{
    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(genFile, &sched))
    {
        fail(genFile + " did not load for the live-edit gate");
        return;
    }

    /* The first stage with a param a number can be set on. */
    thcStage *stage = NULL;
    int idx = -1;

    for (size_t ci = 0; ci < sched.chainCount() && stage == NULL; ci++)
    {
        thcChain *c = sched.chain(ci);

        for (size_t si = 0; si < c->stages.size() && stage == NULL; si++)
        {
            thcStage *s = c->stages[si].get();

            for (int i = 0; i < s->plugin->paramCount(); i++)
            {
                const thcPlugin::ParamInfo *p = s->plugin->paramInfo(i);

                if (p->type != THC_PARAM_FLOAT && p->type != THC_PARAM_INT)
                    continue;

                stage = s;
                idx = i;
                break;
            }
        }
    }

    if (stage == NULL)
    {
        fail(genFile + " has no numeric param to edit; the live-edit gate "
             "needs one");
        return;
    }

    const char *name = stage->plugin->paramInfo(idx)->name.c_str();

    /* Played once, so the store is frozen: everything from here on is an
       edit and not part of the load. */
    render(sched, 1.0, 0.02);

    /* An edit while it is loaded and playing, exactly as the editor
       makes one. A value the file cannot already hold, so "it survived"
       and "it was never set" cannot be told apart by luck. */
    const double edited = stage->plugin->paramInfo(idx)->min +
                          (stage->plugin->paramInfo(idx)->max -
                           stage->plugin->paramInfo(idx)->min) * 0.37 + 0.001;

    stage->params.set(idx, edited);
    sched.reset();

    if (stage->params.get(idx) != edited)
        fail(std::string("a value set after Play did not survive a rewind: ")
             + name + " went back to " +
             std::to_string(stage->params.get(idx)));

    /* A binding made after Play, to a knob the piece declares -- the
       scheduler hands back NULL for a name nobody declared, and binding
       NULL is the unbind. The knob's changed signal is connected once,
       at the bind, and reset() does not disconnect it, so a rewind that
       dropped the binding left a live signal driving a param that no
       longer read through it. */
    if (sched.knobs().empty())
    {
        fail(genFile + " declares no knobs; the live-edit gate needs one");
        return;
    }

    thArg *knob = sched.knobs().begin()->second;

    sched.bindKnob(stage, idx, knob);
    sched.reset();

    if (stage->params.knobBinding(idx) != knob)
        fail(std::string("a knob bound after Play did not survive a "
                         "rewind: ") + name + " is unbound again");

    /* And the unbind, which is an edit like any other. */
    sched.unbindParam(stage, idx);
    sched.reset();

    if (stage->params.knobBinding(idx) != NULL)
        fail(std::string("a param unbound after Play was bound again by a "
                         "rewind: ") + name);

    if (stage->params.get(idx) != edited)
        fail(std::string("the stored value behind an unbound param did not "
                         "survive a rewind: ") + name);
}

static void
checkReplay (const std::map<std::string, thcPlugin *> &plugins,
             thSynth *synth, const std::string &genFile)
{
    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(genFile, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail(genFile + " did not load");
        return;
    }

    if (!loader.hasSeed())
        fail(genFile + " pins no seed; the replay gate needs one");

    std::string first = render(sched, 180.0, 0.02);

    /* reset() must announce itself: the piano roll drops its history on
       this signal, and a reset nobody hears about leaves the previous
       piece's notes on screen as a future that already happened. */
    bool announced = false;
    sigc::connection resetConn = sched.sigReset.connect(
        [&announced] { announced = true; });

    sched.reset();

    if (!announced)
        fail("reset() did not emit sigReset");

    resetConn.disconnect();

    std::string second = render(sched, 180.0, 0.02);

    if (first.empty())
        fail("three minutes of the piece delivered nothing at all");

    if (first != second)
    {
        fail("replay diverged: same file, same seed, different stream");
        showDivergence(first, second, "first ", "second");
    }

    /* The piece exercises the whole seam or this gate is weaker than it
       looks: notes from the eno lines, quantized notes from the
       wildcard, and chanarg events from the drift chain. */
    if (first.find("N ") == std::string::npos)
        fail("no note events in the stream");

    if (first.find("C ") == std::string::npos)
        fail("no chanarg events in the stream -- the drift chain is not "
             "flowing");

    if (first.find("fmin") == std::string::npos)
        fail("the chanarg sink's name never reached delivery");

    /* And the same piece however finely the host steps it.
     *
     * The transport's dt is the host's business -- a 1024-frame window at
     * 44.1 kHz on the desktop, 256 frames at whatever rate a browser's
     * device runs at, 20 ms of wall clock from the Glib timer -- and none
     * of it belongs in what a piece composes. It used to: a composer was
     * ticked at the end of the step its wakeup fell in, so every wake was
     * late by up to a step and the next was scheduled from the late one.
     * Two machines with different sound cards composed different pieces
     * from one file and one seed, which is the one failure a jam cannot
     * see happening (docs/JAM.md). This is what stops it coming back, and
     * what every new composer meets. */
    sched.reset();

    const std::string coarse = render(sched, 180.0, 1024.0 / 44100.0);

    sched.reset();

    const std::string fine = render(sched, 180.0, 256.0 / 48000.0);

    if (coarse != first)
    {
        fail("the piece composes differently at a 1024-frame step");
        showDivergence(first, coarse, "20 ms ", "1024fr");
    }

    if (fine != first)
    {
        fail("the piece composes differently at a 256-frame step");
        showDivergence(first, fine, "20 ms ", "256 fr");
    }
}

/* ---- 4. the planners --------------------------------------------------- */

/* lsystem and evolve emit whole phrases into the future -- the first
 * composers that plan. Two claims worth a gate: the pending heap
 * actually holds a scheduled future right after a tick (the piano
 * roll's ghosted half is drawn from it, and a regression here would be
 * invisible until someone looked), and the planning replays exactly --
 * evolve especially, since a GA that drifted off its seed would corrupt
 * the determinism story in the least debuggable way possible. */

static void
checkPlanners (const std::map<std::string, thcPlugin *> &plugins,
               thSynth *synth)
{
    {
        /* Named, so the failure says which module to go and build
           rather than waving at a category. */
        const char *need[] = { "lsystem", "evolve", "markov", "ca",
                               NULL };
        bool missing = false;

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                missing = true;
            }

        if (missing)
            return;
    }

    /* Unique for the same reason every other scratch here is. */
    std::string tmp = thUtil::tempFile("gencheck-planners-");

    if (tmp.empty())
    {
        fail("could not make a planners scratch file");
        return;
    }

    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "seed 7;\n"
            "chain canopy {\n"
            "    stage src gen::lsystem {\n"
            "        axiom = \"X\"; rules = \"X=F[+X]F[-X]\";\n"
            "        depth = 3; step = 0.15 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 1; };\n"
            "};\n"
            "chain roots {\n"
            "    stage src gen::evolve {\n"
            "        length = 8; population = 8;\n"
            "        step = 0.2 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 2; };\n"
            "};\n"
            "chain dream {\n"
            "    stage teacher gen::lsystem {\n"
            "        axiom = \"X\"; rules = \"X=F[+X]F[-X]\";\n"
            "        depth = 3; step = 0.15 s; hold = 0.2 s;\n"
            "    };\n"
            "    stage student gen::markov {\n"
            "        pass = 0; period = 0.2 s; hold = 0.2 s;\n"
            "    };\n"
            "    sink { channel = 3; };\n"
            "};\n"
            "chain grid {\n"
            "    stage src gen::ca {\n"
            "        rule = 110; width = 8;\n"
            "        period = 0.2 s; hold = 0.1 s;\n"
            "    };\n"
            "    sink { channel = 4; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("the planners piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    /* One tick in: both plugins have committed a phrase, and the future
       is sitting in the pending heap where the roll can see it. */
    sched.start();
    sched.stepTransport(0.05);

    if (sched.peekPending().size() < 5)
        fail("planners scheduled almost nothing ahead; the ghosted "
             "future would be empty");

    sched.stop();
    sched.reset();

    std::string first = render(sched, 30.0, 0.02);

    sched.reset();

    std::string second = render(sched, 30.0, 0.02);

    if (first.empty())
        fail("thirty seconds of planners delivered nothing");

    if (first != second)
        fail("planner replay diverged -- evolution is drawing "
             "randomness from somewhere outside its seed");

    /* The learner and the automaton both spoke: the markov's channel
       proves receive() trained it from its upstream teacher (pass = 0,
       so anything on the third channel is the student's own), and the ca's
       proves the ring is advancing. */
    {
        int perChan[4] = { 0, 0, 0, 0 };
        std::istringstream in(first);
        std::string line;
        double at;
        int chan, note;

        while (std::getline(in, line))
            if (sscanf(line.c_str(), "N %lg %d %d", &at, &chan,
                       &note) == 3 && chan >= 0 && chan < 4)
                perChan[chan]++;

        if (perChan[2] < 5)
            fail("the markov student never dreamed -- receive() is not "
                 "training from upstream");

        if (perChan[3] < 5)
            fail("the cellular automaton never fired");
    }

    /* The GA must not freeze. With a static fitness landscape and two
       protected elites it used to: a local optimum inside a minute,
       then the same bars forever. The boredom tax is what keeps the
       optimum moving, and this holds it down -- deterministic, since
       the piece is seeded, so it either passes always or fails always.
       Group the evolve chain's notes into cycle-length windows and
       count distinct phrases across ~18 cycles. The comparisons below
       are against *delivered* channels, which the engine counts from
       zero -- the file's `channel = 2' is this stream's channel 1. */
    {
        std::istringstream in(first);
        std::string line;
        std::map<int, std::string> cycles;

        while (std::getline(in, line))
        {
            double at;
            int chan, note;

            if (sscanf(line.c_str(), "N %lg %d %d", &at, &chan,
                       &note) == 3 && chan == 1)
            {
                char b[16];

                snprintf(b, sizeof(b), "%d,", note);
                cycles[(int)(at / 1.6)] += b;   /* 8 steps * 0.2s      */
            }
        }

        std::map<std::string, int> distinct;

        for (std::map<int, std::string>::iterator i = cycles.begin();
             i != cycles.end(); ++i)
            distinct[i->second]++;

        if (distinct.size() < 4)
            fail("evolve froze: fewer than four distinct phrases in "
                 "thirty seconds -- the boredom tax is not biting");
    }

    std::filesystem::remove(tmp);
}

/* ---- 5. live input ----------------------------------------------------- */

/* The injectMidiEvent path end to end: a chain with `input midi' hears
 * presses and releases, an arp stage turns held keys into steps, a
 * bare input chain passes the performance straight through -- and all
 * of it replays exactly when the same events arrive at the same
 * transport times, which is what makes recorded performances a future
 * feature instead of a rewrite. Also pins the decided open question:
 * keys pressed on a STOPPED transport sound immediately instead of
 * waiting for Play behind a frozen clock. */

static void
checkLiveInput (const std::map<std::string, thcPlugin *> &plugins,
                thSynth *synth)
{
    if (plugins.find("arp") == plugins.end())
    {
        fail("arp module missing; build the plugins first");
        return;
    }

    /* Unique for the same reason every other scratch here is. */
    std::string tmp = thUtil::tempFile("gencheck-live-");

    if (tmp.empty())
    {
        fail("could not make a live-input scratch file");
        return;
    }

    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "seed 11;\n"
            "chain hands {\n"
            "    input midi;\n"
            "    stage a gen::arp { period = 0.1 s; hold = 0.08 s; };\n"
            "    sink { channel = 1; };\n"
            "};\n"
            "chain thru {\n"
            "    input midi;\n"
            "    sink { channel = 2; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("the live-input piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    /* One scripted performance, in virtual time. Routing is by the
       chains' sink channels, and these are the *engine's* numbers: the
       file writes 1 and 2, the loader hands over 0 and 1, and injection
       and delivery both speak the latter. Channel 0 reaches `hands'
       (the arp), channel 1 reaches `thru'. */
    auto press = [&sched](int chan, int note, int vel)
    {
        thcEvent ev = {};

        ev.type = THC_EV_NOTE;
        ev.u.note.level = 1;
        ev.at = sched.now();
        ev.channel = chan;
        ev.u.note.note = note;
        ev.u.note.velocity = vel;
        ev.u.note.duration = 0;
        sched.injectMidiEvent(ev);
    };
    auto release = [&sched](int chan, int note)
    {
        thcEvent ev = {};

        ev.type = THC_EV_NOTEOFF;
        ev.at = sched.now();
        ev.channel = chan;
        ev.u.note.note = note;
        sched.injectMidiEvent(ev);
    };

    auto perform = [&]() -> std::string
    {
        std::string tape;
        sigc::connection conn = sched.sigDelivered.connect(
            [&tape](const thcEvent &ev)
            {
                char b[96];

                if (ev.type == THC_EV_NOTE)
                    snprintf(b, sizeof(b), "N %.17g %d %d %d\n", ev.at,
                             ev.channel, ev.u.note.note,
                             ev.u.note.velocity);
                else if (ev.type == THC_EV_NOTEOFF)
                    snprintf(b, sizeof(b), "O %.17g %d %d\n", ev.at,
                             ev.channel, ev.u.note.note);
                else
                    b[0] = 0;

                tape += b;
            });

        sched.start();

        while (sched.now() < 3.0)
        {
            sched.stepTransport(0.02);

            /* The scripted hands, at exact virtual moments. */
            double t = sched.now();

            if (t >= 0.10 && t < 0.12) { press(0, 60, 90); }
            if (t >= 0.14 && t < 0.16)
            {
                press(0, 64, 70);
                press(0, 67, 50);
            }
            if (t >= 0.50 && t < 0.52) { press(1, 48, 111); }
            if (t >= 1.00 && t < 1.02) { release(1, 48); }
            if (t >= 2.00 && t < 2.02)
            {
                release(0, 60);
                release(0, 64);
                release(0, 67);
            }
        }

        sched.stop();
        conn.disconnect();

        return tape;
    };

    std::string first = perform();

    sched.reset();

    std::string second = perform();

    if (first != second)
        fail("a scripted performance replayed differently");

    /* The arp stepped through exactly the held pitches, inheriting the
       performance's velocities (vel = 0 means as played). */
    int arpNotes = 0;
    bool wrongPitch = false, wrongVel = false, thruOk = false;
    {
        std::istringstream in(first);
        std::string line;
        double at;
        int chan, note, vel;

        while (std::getline(in, line))
            if (sscanf(line.c_str(), "N %lg %d %d %d", &at, &chan, &note,
                       &vel) == 4)
            {
                if (chan == 0)
                {
                    arpNotes++;

                    if (note != 60 && note != 64 && note != 67)
                        wrongPitch = true;

                    if (vel != 90 && vel != 70 && vel != 50)
                        wrongVel = true;
                }

                if (chan == 1 && note == 48 && vel == 111)
                    thruOk = true;
            }
    }

    if (arpNotes < 10)
        fail("the arp barely stepped; held notes are not reaching it");

    if (wrongPitch)
        fail("the arp emitted a pitch nobody held");

    if (wrongVel)
        fail("vel = 0 did not inherit the performance's velocities");

    if (!thruOk)
        fail("the pass-through chain never delivered the raw press");

    /* Keys on a stopped transport sound immediately. */
    {
        sched.reset();

        int now = 0;
        sigc::connection conn = sched.sigDelivered.connect(
            [&now](const thcEvent &ev)
            {
                if (ev.type == THC_EV_NOTE)
                    now++;
            });

        press(1, 72, 100);       /* the bare chain: nothing swallows it */

        if (now < 1)
            fail("a key pressed on a stopped transport made no sound");

        release(1, 72);
        conn.disconnect();
        sched.reset();
    }

    std::filesystem::remove(tmp);
}

/* ---- 6. the editor's splices ------------------------------------------ */

/* thcGenEdit's whole promise is that an edit touches the bytes it names
 * and nothing else -- so every comment in the file survives any sequence
 * of edits, an edit that changes nothing writes nothing, and the file
 * after each edit still loads. Exercised on a scratch copy of the real
 * piece, because the real piece is where the comments are. */

static std::string
slurp (const std::string &path)
{
    std::ifstream in(path.c_str());

    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static std::vector<std::string>
commentLines (const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line))
    {
        size_t sp = line.find_first_not_of(" \t");

        if (sp != std::string::npos && line[sp] == '#')
            out.push_back(line.substr(sp));
    }

    return out;
}

static void
editOk (thcGenEdit::Result r, const std::string &why, const char *what)
{
    if (r != thcGenEdit::OK)
        fail(std::string(what) + ": " + why + " (" +
             thcGenEdit::resultText(r) + ")");
}

static void
checkEdits (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth, const std::string &genFile)
{
    /* Unique for the same reason expectReject's scratch is: parallel
       gencheck runs must not edit each other's copy. */
    std::string path = thUtil::tempFile("gencheck-edit-");

    if (path.empty())
    {
        fail("could not make a scratch copy for editing");
        return;
    }

    std::filesystem::copy_file(genFile, path,
        std::filesystem::copy_options::overwrite_existing);
    std::vector<std::string> comments = commentLines(slurp(path));
    std::string why;

    if (comments.empty())
        fail("the shipped piece has no comments; this check needs them");

    /* Reading the structure back. */
    thcGenEdit::Doc doc;

    editOk(thcGenEdit::describe(path, doc, why), why, "describe");

    if (doc.chains.size() != 9)
        fail("describe found the wrong number of chains");

    if (!doc.hasSeed || doc.seed != 1978)
        fail("describe missed the pinned seed");

    if (doc.knobs.size() != 1 || doc.knobs[0].name != "density")
        fail("describe missed the density knob");

    if (doc.chains[0].stages.size() != 1 ||
        doc.chains[0].stages[0].params.size() < 2 ||
        doc.chains[0].stages[0].params[1].valueText != "23.9 s")
        fail("describe did not keep the authored '23.9 s'");

    /* The instrument block, and the sinks that bind to it by name. */
    if (doc.instruments.size() != 1 || doc.instruments[0].name != "pad" ||
        doc.instruments[0].dsp != "amb01.dsp")
        fail("describe missed the pad instrument");
    else
    {
        bool sawAttack = false;

        for (size_t i = 0; i < doc.instruments[0].values.size(); i++)
            if (doc.instruments[0].values[i].name == "a")
            {
                sawAttack = true;

                /* Authored, not folded -- the same promise the stage
                   params get, and the one that makes an instrument
                   block readable at all. */
                if (doc.instruments[0].values[i].valueText != "900 ms")
                    fail("describe did not keep the authored '900 ms': " +
                         doc.instruments[0].values[i].valueText);
            }

        if (!sawAttack)
            fail("describe missed the pad's attack");
    }

    if (doc.chains[0].sinks.size() != 1 ||
        doc.chains[0].sinks[0].instrument != "pad" ||
        doc.chains[0].sinks[0].channel != 0)
        fail("describe did not read the sink's instrument binding");

    /* An edit that changes nothing writes nothing. */
    std::string before = slurp(path);

    editOk(thcGenEdit::setParam(path, "loop_f3", 0, "period", "23.9 s",
                                why), why, "no-op setParam");

    if (slurp(path) != before)
        fail("writing the value already there changed the file");

    /* One of everything, on the scratch copy. */
    editOk(thcGenEdit::setParam(path, "loop_f3", 0, "period", "21.5 s",
                                why), why, "setParam replace");
    editOk(thcGenEdit::setParam(path, "loop_f3", 0, "vel_jitter", "12",
                                why), why, "setParam replace 2");
    editOk(thcGenEdit::setKnobValue(path, "density", 0.7, why), why,
           "setKnobValue");
    editOk(thcGenEdit::setKnobMeta(path, "density", 0, 1, "How often",
                                   why), why, "setKnobMeta");
    editOk(thcGenEdit::setInfo(path, "author", "gencheck", why), why,
           "setInfo");
    editOk(thcGenEdit::setSeed(path, 4242, why), why, "setSeed");
    editOk(thcGenEdit::setTempo(path, 90, why), why, "setTempo");
    editOk(thcGenEdit::addKnob(path, "shimmer", 0.5, 0, 1, "Shimmer",
                               why), why, "addKnob");
    editOk(thcGenEdit::setParam(path, "loop_ab3", 0, "prob", "@shimmer",
                                why), why, "bind to new knob");
    editOk(thcGenEdit::addScale(path, "pent", "C4 D4 E4 G4 A4", why),
           why, "addScale");
    editOk(thcGenEdit::setScale(path, "pent", "C3 D3 E3 G3 A3", why),
           why, "setScale");

    {
        std::vector<thcGenEdit::PresetValue> vals;
        thcGenEdit::PresetValue pv;

        pv.name = "res";  pv.value = 0.4;  vals.push_back(pv);
        pv.name = "fmin"; pv.value = 0.1;  vals.push_back(pv);

        editOk(thcGenEdit::addPreset(path, "dim", vals, why), why,
               "addPreset");
    }

    editOk(thcGenEdit::setPresetValue(path, "dim", "res", 0.55, why), why,
           "setPresetValue");
    editOk(thcGenEdit::addPresetValue(path, "dim", "fmax", 0.8, why), why,
           "addPresetValue");
    editOk(thcGenEdit::removePresetValue(path, "dim", "fmin", why), why,
           "removePresetValue");

    /* A preset written on one line, which addPreset never produces and a
       person writes all the time. Its `}' shares a line with its
       `preset', so the start of that line is *before* the block -- an
       insert aimed there puts the new component above the statement it
       belongs to, and the file stops loading. Written by hand here for
       exactly that reason: this editor's own output would never have
       found it. */
    {
        std::string text = slurp(path);

        text += "\npreset flat { one = 1; };\n";

        std::ofstream out(path.c_str(), std::ios::trunc);

        out << text;
    }

    editOk(thcGenEdit::addPresetValue(path, "flat", "two", 0.25, why), why,
           "addPresetValue on a one-liner");

    {
        const std::string after = slurp(path);
        const size_t at = after.find("preset flat");

        if (at == std::string::npos)
            fail("the one-line preset survived at all");
        else
        {
            const size_t eol = after.find('\n', at);
            const std::string line = after.substr(at, eol - at);

            if (line.find("two = 0.25;") == std::string::npos)
                fail("the new component did not land inside the one-line "
                     "block: " + line);
        }
    }

    std::vector<std::pair<std::string, std::string> > params;

    params.push_back(std::make_pair(std::string("notes"),
                                    std::string("pent")));
    params.push_back(std::make_pair(std::string("period"),
                                    std::string("2 beats")));
    params.push_back(std::make_pair(std::string("prob"),
                                    std::string("0.5")));

    editOk(thcGenEdit::addChain(path, "pulse", 2, "", "src", "gen",
                                "eno_line", params, why), why, "addChain");

    /* And one whose sink binds to the piece's own instrument, which is
       what a new chain in an instrument-carrying piece should do -- a
       `channel = 1' here would take the pad's channel and move it.
       Its own params, naming no scale, so that the reference count
       removeScale checks below stays about the chain above. */
    {
        std::vector<std::pair<std::string, std::string> > bparams;

        bparams.push_back(std::make_pair(std::string("notes"),
                                         std::string("\"C4\"")));

        editOk(thcGenEdit::addChain(path, "bound", 1, "pad", "src", "gen",
                                    "eno_line", bparams, why), why,
               "addChain onto an instrument");
    }

    {
        thcGenEdit::Doc mid;

        thcGenEdit::describe(path, mid, why);

        const thcGenEdit::Chain *bound = NULL;

        for (size_t i = 0; i < mid.chains.size(); i++)
            if (mid.chains[i].name == "bound")
                bound = &mid.chains[i];

        if (bound == NULL || bound->sinks.size() != 1 ||
            bound->sinks[0].instrument != "pad")
            fail("addChain did not write the instrument it was given");
    }

    std::vector<std::pair<std::string, std::string> > qparams;

    qparams.push_back(std::make_pair(std::string("scale"),
                                     std::string("pent")));

    editOk(thcGenEdit::addStage(path, "pulse", "q", "xform", "quantize",
                                qparams, why), why, "addStage");
    editOk(thcGenEdit::addSink(path, "pulse", 5, "", "cutoff", why), why,
           "addSink");
    editOk(thcGenEdit::setSink(path, "pulse", 1, 6, "", "", why), why,
           "setSink to note sink");
    editOk(thcGenEdit::setSink(path, "pulse", 0, 2, "", "bright", why), why,
           "setSink add chanarg");

    /* Off the piece's own instrument onto a bare channel and back --
       the two spellings of a target, and the one edit that replaces a
       statement rather than a value inside one. */
    editOk(thcGenEdit::setSink(path, "loop_f3", 0, 9, "", "", why), why,
           "setSink instrument -> channel");

    {
        thcGenEdit::Doc mid;

        thcGenEdit::describe(path, mid, why);

        if (mid.chains.empty() || mid.chains[0].sinks.empty() ||
            !mid.chains[0].sinks[0].instrument.empty() ||
            mid.chains[0].sinks[0].channel != 9)
            fail("setSink left the instrument behind when it wrote a "
                 "channel");
    }

    editOk(thcGenEdit::setSink(path, "loop_f3", 0, 9, "pad", "", why), why,
           "setSink channel -> instrument");

    {
        thcGenEdit::Doc mid;

        thcGenEdit::describe(path, mid, why);

        if (mid.chains.empty() || mid.chains[0].sinks.empty() ||
            mid.chains[0].sinks[0].instrument != "pad" ||
            mid.chains[0].sinks[0].channel != 0)
            fail("setSink left the channel behind when it wrote an "
                 "instrument");
    }

    /* A sink cannot be pointed at an instrument nobody declared: every
       state this editor writes has to load. */
    if (thcGenEdit::setSink(path, "loop_f3", 0, 1, "ghost", "", why) ==
        thcGenEdit::OK)
        fail("a sink was pointed at an undeclared instrument");

    /* Nor at one declared *below* it. The loader resolves names in file
       order, so a chain above its instrument cannot name it -- and a
       file is perfectly free to be written that way round. Checking
       only that the name exists somewhere is how this wrote a file it
       could not then load. Its own scratch, because the shape the bug
       needs is not the shape the shipped piece has. */
    {
        std::string below = thUtil::tempFile("gencheck-below-");

        if (below.empty())
            fail("could not make a scratch file for the ordering check");
        else
        {
            {
                std::ofstream out(below.c_str(), std::ios::trunc);

                out << "chain c { stage s gen::eno_line { };"
                       " sink { channel = 9; }; };\n"
                       "instrument late { dsp \"amb01.dsp\"; };\n";
            }

            std::string was = slurp(below);

            if (thcGenEdit::setSink(below, "c", 0, 9, "late", "", why) ==
                thcGenEdit::OK)
                fail("a sink was pointed at an instrument declared below "
                     "it, which is a file that will not load");

            if (slurp(below) != was)
                fail("the refused ordering edit wrote to the file anyway");

            std::filesystem::remove(below);
        }
    }

    /* A block written on one line, which is a shape a hand-written piece
       uses freely and this writer used to break. Both of these append a
       statement to the end of a block, and both found the place by taking
       the start of the line the closing `}' is on -- which, when
       something shares that line, is a point *before* the block's own
       statement. So the sink landed above the chain it belonged to and
       the param above the stage, at top level, and the file came back
       "written" and no longer loaded: `unknown statement 'sink''. What is
       checked is the property, which is that what this writes, loads. */
    {
        std::string one = thUtil::tempFile("gencheck-oneline-");

        if (one.empty())
            fail("could not make a scratch file for the one-liner check");
        else
        {
            {
                std::ofstream out(one.c_str(), std::ios::trunc);

                out << "instrument pad { dsp \"amb01.dsp\"; };\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = pad; }; };\n";
            }

            editOk(thcGenEdit::addSink(one, "c", 1, "pad", "res", why), why,
                   "addSink into a chain written on one line");

            editOk(thcGenEdit::setParam(one, "c", 0, "vel", "80", why), why,
                   "setParam into a stage written on one line");

            /* Still one line: a writer that reformatted someone's file
               around its own edit would be an edit nobody asked for. */
            const std::string after = slurp(one);

            if (after.find("\n    sink") != std::string::npos ||
                after.find("\n        vel") != std::string::npos)
                fail("a one-line block was broken across lines: " + after);

            if (after.find("chanarg = \"res\"") == std::string::npos ||
                after.find("vel = 80") == std::string::npos)
                fail("the one-line edits did not land: " + after);

            {
                thcScheduler sched(synth);
                thcGenLoader loader(plugins);

                drainSynth();

                if (!loader.load(one, &sched))
                {
                    for (size_t i = 0; i < loader.errors().size(); i++)
                        fprintf(stderr, "gencheck: %s\n",
                                loader.errors()[i].c_str());

                    fail("the file this writer edited on one line no "
                         "longer loads");
                }
            }

            std::filesystem::remove(one);
        }
    }

    editOk(thcGenEdit::setChainInput(path, "pulse", true, why), why,
           "setChainInput on");
    editOk(thcGenEdit::setChainInput(path, "pulse", false, why), why,
           "setChainInput off");
    editOk(thcGenEdit::renameChain(path, "pulse", "pulse2", why), why,
           "renameChain");
    editOk(thcGenEdit::moveStage(path, "wildcard", 0, 1, why), why,
           "moveStage");
    editOk(thcGenEdit::moveStage(path, "wildcard", 1, 0, why), why,
           "moveStage back");

    int rewritten = 0;

    editOk(thcGenEdit::removeKnob(path, "shimmer", 0.5, rewritten, why),
           why, "removeKnob");

    if (rewritten != 1)
        fail("removeKnob did not rewrite the one binding to it");

    /* A knob read from inside an instrument is a binding too, and a
       removal that walked only the stages left a dangling `@name' and a
       file that no longer loaded -- the exact failure the rewriting is
       for, in the half of the language that grew after it. The unit
       comes along, because a bare number on a folded chanarg is refused.
       Its own scratch: the shipped piece has no such binding, which is
       the point. */
    {
        std::string bound = thUtil::tempFile("gencheck-knobinst-");

        if (bound.empty())
            fail("could not make a scratch file for the knob removal");
        else
        {
            {
                std::ofstream out(bound.c_str(), std::ios::trunc);

                out << "@tail = 1800;\n@tail.min = 200;\n@tail.max = 5000;\n"
                       "instrument pad {\n"
                       "    dsp \"amb01.dsp\";\n"
                       "    r = @tail ms;\n"
                       "    fmin = @tail;\n"
                       "};\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = pad; }; };\n";
            }

            /* Before removing it: a sink cannot be pointed at an arg
               that knob already drives, because the loader refuses that
               and every state this editor writes has to load. Both
               writers, since addSink and setSink share the check but
               not the call site. */
            if (thcGenEdit::addSink(bound, "c", 1, "pad", "fmin", why) ==
                thcGenEdit::OK)
                fail("addSink wrote a sink that fights a knob");

            if (thcGenEdit::setSink(bound, "c", 0, 1, "pad", "r", why) ==
                thcGenEdit::OK)
                fail("setSink wrote a sink that fights a knob");

            /* An arg the instrument leaves alone is still fair game. */
            editOk(thcGenEdit::addSink(bound, "c", 1, "pad", "res", why),
                   why, "addSink onto an arg no knob drives");

            int n = 0;

            editOk(thcGenEdit::removeKnob(bound, "tail", 1800, n, why), why,
                   "removeKnob bound into an instrument");

            if (n != 2)
            {
                std::ostringstream s;

                s << "removeKnob rewrote " << n
                  << " instrument bindings, not 2";
                fail(s.str());
            }

            const std::string after = slurp(bound);

            if (after.find("@tail") != std::string::npos)
                fail("removeKnob left a dangling knob reference behind");

            if (after.find("r = 1800 ms;") == std::string::npos)
                fail("removeKnob dropped the unit off a binding it "
                     "rewrote: " + after);

            thcGenEdit::Doc back;

            if (thcGenEdit::describe(bound, back, why) != thcGenEdit::OK)
                fail("the file after removeKnob no longer reads");

            std::filesystem::remove(bound);
        }
    }

    editOk(thcGenEdit::removeScale(path, "pent", rewritten, why), why,
           "removeScale");

    if (rewritten != 2)
        fail("removeScale did not inline its two references");

    editOk(thcGenEdit::removeStage(path, "pulse2", 1, why), why,
           "removeStage");
    editOk(thcGenEdit::removeSink(path, "pulse2", 1, why), why,
           "removeSink");
    editOk(thcGenEdit::clearSeed(path, why), why, "clearSeed");
    editOk(thcGenEdit::clearTempo(path, why), why, "clearTempo");
    editOk(thcGenEdit::removeChain(path, "pulse2", why), why,
           "removeChain");

    /* Guard rails. */
    if (thcGenEdit::removeSink(path, "drift", 0, why) !=
        thcGenEdit::REFUSED)
        fail("removing a chain's last sink was not refused");

    if (thcGenEdit::setParam(path, "loop_f3", 0, "period", "20 furlongs",
                             why) != thcGenEdit::UNWRITABLE)
        fail("a unit the lexer does not know was accepted");

    /* A legal channel, so what this refuses is the duplicate name and
       not the target -- the check used to pass channel 0 and was
       answered by the range check before it ever reached the name. */
    if (thcGenEdit::addChain(path, "loop_f3", 1, "", "s", "gen", "eno_line",
            std::vector<std::pair<std::string, std::string> >(), why) !=
        thcGenEdit::REFUSED)
        fail("a duplicate chain name was accepted");

    /* The preset guard rails, all three of which exist because the state
       they would leave behind does not load. */
    if (thcGenEdit::addPreset(path, "empty",
            std::vector<thcGenEdit::PresetValue>(), why) !=
        thcGenEdit::REFUSED)
        fail("a preset that sets nothing was accepted");

    {
        std::vector<thcGenEdit::PresetValue> one;
        thcGenEdit::PresetValue pv;

        pv.name = "res"; pv.value = 0.1; one.push_back(pv);

        if (thcGenEdit::addPreset(path, "dim", one, why) !=
            thcGenEdit::REFUSED)
            fail("a duplicate preset name was accepted");
    }

    /* Unlike a scale, a preset reference cannot be inlined on the way
       out: the format has no literal form for a chanarg vector. So a
       preset something still names is refused, and the message says
       which stage -- "it is used" without "by what" sends the reader
       through the file. */
    {
        std::vector<thcGenEdit::PresetValue> vals;
        thcGenEdit::PresetValue pv;

        pv.name = "res"; pv.value = 0.9; vals.push_back(pv);

        editOk(thcGenEdit::addPreset(path, "held", vals, why), why,
               "addPreset held");

        /* `held' sets one thing, so removing it would leave a preset
           that sets nothing -- which does not load, and every state this
           editor writes has to. */
        if (thcGenEdit::removePresetValue(path, "held", "res", why) !=
            thcGenEdit::REFUSED)
            fail("removing a preset's last value was not refused");

        std::vector<std::pair<std::string, std::string> > mparams;

        mparams.push_back(std::make_pair(std::string("from"),
                                         std::string("held")));
        mparams.push_back(std::make_pair(std::string("to"),
                                         std::string("dim")));

        editOk(thcGenEdit::addChain(path, "sweep", 4, "", "m", "gen", "morph",
                                    mparams, why), why, "addChain morph");

        if (thcGenEdit::removePreset(path, "held", why) !=
            thcGenEdit::REFUSED)
            fail("removing a preset a stage still names was not refused");
        else if (why.find("sweep's stage m") == std::string::npos)
            fail("the refusal did not say which stage still names it: " +
                 why);

        editOk(thcGenEdit::removeChain(path, "sweep", why), why,
               "removeChain sweep");
        editOk(thcGenEdit::removePreset(path, "held", why), why,
               "removePreset");
    }

    /* After all of that: every comment intact, and the file loads. */
    std::string after = slurp(path);
    std::vector<std::string> commentsAfter = commentLines(after);

    for (size_t i = 0; i < comments.size(); i++)
    {
        bool found = false;

        for (size_t j = 0; j < commentsAfter.size(); j++)
            if (commentsAfter[j] == comments[i])
                found = true;

        if (!found)
            fail("a comment was lost in editing: " + comments[i]);
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n",
                        loader.errors()[i].c_str());

            fail("the edited file no longer loads");
        }

        if (loader.hasSeed())
            fail("clearSeed left a seed behind");
    }

    /* The edits round-trip through describe: the changed period reads
       back as authored. */
    editOk(thcGenEdit::describe(path, doc, why), why, "describe after");

    if (doc.chains[0].stages[0].params[1].valueText != "21.5 s")
        fail("the edited period did not read back as '21.5 s'");

    if (doc.author != "gencheck")
        fail("the edited author did not read back");

    /* The preset reads back as the vector it now is, in order: `res'
       edited, `fmin' removed, `fmax' appended at the end rather than in
       some canonical slot -- a preset's order is its author's. */
    {
        const thcGenEdit::Preset *dim = NULL;

        for (size_t i = 0; i < doc.presets.size(); i++)
            if (doc.presets[i].name == "dim")
                dim = &doc.presets[i];

        if (dim == NULL)
            fail("describe did not read the preset back");
        else if (dim->values.size() != 2 ||
                 dim->values[0].name != "res" ||
                 dim->values[0].value != 0.55 ||
                 dim->values[1].name != "fmax")
            fail("the edited preset did not read back as edited");
    }

    std::filesystem::remove(path);
}

/* ---- 6. presets, the wildcard sink, and morph -------------------------- */

/* The piece composes the instrument as well as the notes.
 * Three things have to hold together for that, and none of them is
 * provable from any other section here.
 *
 * A preset has to arrive at the plugin resolved -- the same bargain
 * NOTESET made, and the reason no composer has ever parsed a note name.
 * The `*' sink has to deliver each component under its own name, because
 * a vector routed through a sink that renames everything arrives as one
 * knob taking three values in turn. And a morph has to replay exactly:
 * it draws no randomness at all, so if this one ever diverges the cause
 * is the scheduler and not the plugin, which makes it a sharper tripwire
 * than a seeded composer would be.
 */
static void
checkPresets (const std::map<std::string, thcPlugin *> &plugins,
              thSynth *synth)
{
    if (plugins.find("morph") == plugins.end())
    {
        fail("the 'morph' module is missing; build the plugins first");
        return;
    }

    /* --- the rejections the format promises --- */

    expectReject(plugins, synth, "no-such-preset",
        "chain c { stage s gen::morph { from = ghost; to = ghost; };"
        " sink { channel = 1; chanarg = \"*\"; }; };",
        "no preset called 'ghost'");

    expectReject(plugins, synth, "duplicate-preset",
        "preset a { x = 1; };\npreset a { x = 2; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "already declared");

    expectReject(plugins, synth, "empty-preset",
        "preset a { };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "sets nothing");

    /* A preset is a vector or it is nothing: interpolating towards a
       component whose value depends on where a slider happens to be is
       not a preset, it is an expression with a value right now. */
    expectReject(plugins, synth, "knob-in-preset",
        "@k = 1;\npreset a { x = @k; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "cannot be a knob");

    expectReject(plugins, synth, "preset-set-twice",
        "preset a { x = 1; x = 2; };\n"
        "chain c { stage s gen::morph { }; sink { channel = 1; }; };",
        "sets 'x' twice");

    /* A preset param takes a name, not the resolved text: a vector
       spelled inline cannot be morphed towards or saved under a name,
       which is the whole reason the noun exists. */
    expectReject(plugins, synth, "preset-as-string",
        "chain c { stage s gen::morph { from = \"x=1\"; };"
        " sink { channel = 1; }; };",
        "declare it with `preset'");

    expectReject(plugins, synth, "preset-as-number",
        "chain c { stage s gen::morph { from = 3; };"
        " sink { channel = 1; }; };",
        "wants a preset name");

    /* A sink pointed at a name no .dsp could declare would fail silently
       at delivery, which is a long way from the typo. */
    /* The bottom of the old range. A file written when channels counted
       from zero is indistinguishable from one written for 1-16 *except*
       here, so this is the only place the change can be caught rather
       than silently transposing a piece -- and the message says what
       happened rather than only that a number was out of range. */
    expectReject(plugins, synth, "channel-zero",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 0; }; };",
        "channel is 1-16 now");

    expectReject(plugins, synth, "channel-seventeen",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 17; }; };",
        "whole number, 1-16");

    expectReject(plugins, synth, "bad-sink-name",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; chanarg = \"cut off\"; }; };",
        "is not a chanarg name");

    /* `fx.' is a prefix and not punctuation a name may contain: what
       follows it still has to be something a .dsp could declare. */
    expectReject(plugins, synth, "bad-sink-name-fx",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; chanarg = \"fx.cut off\"; }; };",
        "is not a chanarg name");

    expectReject(plugins, synth, "bare-fx-prefix",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; chanarg = \"fx.\"; }; };",
        "is not a chanarg name");

    /* And `fx.*' is not a form. A `*' sink keeps the name the event
       arrived with, so there is nothing here for a prefix to go in
       front of -- a composer that wants an effect writes `fx.' on the
       event itself. */
    expectReject(plugins, synth, "fx-wildcard",
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; chanarg = \"fx.*\"; }; };",
        "is not a chanarg name");

    /* --- what it does when it is right --- */

    std::string tmp = thUtil::tempFile("gencheck-presets-");

    if (tmp.empty())
    {
        fail("could not make a presets scratch file");
        return;
    }

    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        /* `arrive' names only what moves. A component one preset
           mentions and the other does not must hold still, which is what
           lets a target be a correction rather than a restatement. */
        out <<
            "seed 11;\n"
            "preset depart { res = 0.2; fmin = 0.10; fmax = 0.30; };\n"
            "preset arrive { fmax = 0.90; };\n"
            "chain sweep {\n"
            "    stage m gen::morph {\n"
            "        from = depart; to = arrive;\n"
            "        time = 4 s; steps = 9; curve = 1; mode = 0;\n"
            "    };\n"
            "    sink { channel = 6; chanarg = \"*\"; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("the presets piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    const std::string first = render(sched, 6.0, 0.02);

    sched.reset();

    const std::string second = render(sched, 6.0, 0.02);

    if (first != second)
        fail("a morph replayed differently; it draws no randomness at "
             "all, so this is the scheduler");

    /* Each component under its own name. Without the `*' sink all three
       would arrive as whatever one name the sink carried. */
    if (first.find(" res ") == std::string::npos ||
        first.find(" fmin ") == std::string::npos ||
        first.find(" fmax ") == std::string::npos)
        fail("the wildcard sink did not deliver each component under its "
             "own name");

    /* Routing still belongs to the piece: the sink's channel overwrites
       whatever the plugin put in the event. */
    if (first.find("C ") != std::string::npos &&
        first.find(" 5 ") == std::string::npos)
        fail("the sink's channel did not reach delivery");

    /* The endpoints, exactly. A sweep that stopped at 0.98 of the way
       would leave the instrument almost at the preset the file named,
       forever -- and 0.9 is the only value `arrive' asks for. */
    if (first.find("fmax 0.30000001192092896") == std::string::npos)
        fail("the morph did not start at the preset it departs from");

    if (first.find("fmax 0.89999997615814209") == std::string::npos)
        fail("the morph did not arrive exactly at the preset it names");

    /* `res' is in `depart' and not in `arrive', so it must be emitted
       and must never move. */
    if (first.find("res 0.20000000298023224") == std::string::npos)
        fail("a component only one preset names was not emitted");

    /* ...and must never move, on any of the nine steps. Counted rather
       than spot-checked: "it was 0.2 at the start" and "it was 0.2
       throughout" are different claims and only the second one is the
       rule being stated. */
    {
        size_t seen = 0, held = 0;

        for (size_t at = first.find("res "); at != std::string::npos;
             at = first.find("res ", at + 1))
        {
            seen++;

            if (first.compare(at, strlen("res 0.20000000298023224"),
                              "res 0.20000000298023224") == 0)
                held++;
        }

        if (seen == 0 || seen != held)
            fail("a component only one preset names did not hold still: " +
                 std::to_string(held) + " of " + std::to_string(seen) +
                 " emissions were the value it was given");
    }

    std::filesystem::remove(tmp);

    /* --- the GA over the same vectors --- */

    if (plugins.find("breed") == plugins.end())
    {
        fail("the 'breed' module is missing; build the plugins first");
        return;
    }

    tmp = thUtil::tempFile("gencheck-breed-");

    if (tmp.empty())
    {
        fail("could not make a breed scratch file");
        return;
    }

    {
        /* spread = 0, so the corridor is exactly the interval the two
           presets span and the bound below is an equality rather than an
           estimate. `hum' is named by one preset only: it has nowhere to
           travel and must still be emitted, held at the value it was
           given. */
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "seed 91;\n"
            "preset shut { res = 0.9; fmin = 0.05; hum = 0.4; };\n"
            "preset wide { res = 0.3; fmin = 0.25; };\n"
            "chain search {\n"
            "    stage g gen::breed {\n"
            "        from = shut; toward = wide;\n"
            "        population = 12; mutation = 0.2; elites = 2;\n"
            "        spread = 0; aim = 1; drift = 0.5; reach = 0.25;\n"
            "        period = 0.5 s;\n"
            "    };\n"
            "    sink { channel = 8; chanarg = \"*\"; };\n"
            "};\n";
    }

    thcScheduler bsched(synth);
    thcGenLoader bloader(plugins);

    if (!bloader.load(tmp, &bsched))
    {
        for (size_t i = 0; i < bloader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", bloader.errors()[i].c_str());

        fail("the breed piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    const std::string bfirst = render(bsched, 20.0, 0.02);

    bsched.reset();

    const std::string bsecond = render(bsched, 20.0, 0.02);

    /* A GA drifting off its seed would be the least debuggable
       corruption of the replay story, which is why evolve has this gate
       and why this one does too. */
    if (bfirst != bsecond)
        fail("a breed replayed differently; same file, same seed");

    if (bfirst.find("C ") == std::string::npos)
        fail("the breed emitted nothing at all");

    /* The first thing played is `from', exactly.
     *
     * `from' is documented as where the population starts, and a piece
     * that names a starting timbre should hear it before it hears what
     * became of it. This was the corridor's midpoint, which whenever a
     * target is named is neither `from' nor near it -- and nothing said
     * so, because nothing looked at the first event. */
    {
        std::istringstream lines(bfirst);
        std::string line;
        bool checked = false, wrong = false;

        while (std::getline(lines, line) && !checked)
        {
            if (line.empty() || line[0] != 'C')
                continue;

            std::istringstream f(line);
            std::string kind, name;
            double at = 0, value = 0;
            int chan = 0;

            f >> kind >> at >> chan >> name >> value;

            if (name != "res")
                continue;

            checked = true;

            /* shut sets res = 0.9; wide sets it to 0.3. The midpoint
               this used to play is 0.6. */
            if (fabs(value - 0.9) > 1e-5)
                wrong = true;
        }

        if (!checked)
            fail("the breed never emitted the component to check");
        else if (wrong)
            fail("the breed did not start at the preset `from' names");
    }

    /* The corridor, which is the whole "declared surface is consent"
     * argument stated as arithmetic: a gene may travel between what the
     * two presets give it and no further, and no component neither
     * preset names can appear. This is the property that stops a search
     * reaching past what an instrument was offered for. */
    {
        bool strayed = false, unknown = false, sawHum = false, humMoved = false;

        std::istringstream lines(bfirst);
        std::string line;

        while (std::getline(lines, line))
        {
            if (line.empty() || line[0] != 'C')
                continue;

            std::istringstream f(line);
            std::string kind, name;
            double at = 0, value = 0;
            int chan = 0;

            f >> kind >> at >> chan >> name >> value;

            double lo = 0, hi = 0;

            if (name == "res")       { lo = 0.3;  hi = 0.9;  }
            else if (name == "fmin") { lo = 0.05; hi = 0.25; }
            else if (name == "hum")
            {
                sawHum = true;

                if (fabs(value - 0.4) > 1e-6)
                    humMoved = true;

                continue;
            }
            else { unknown = true; continue; }

            if (value < lo - 1e-6 || value > hi + 1e-6)
                strayed = true;
        }

        if (unknown)
            fail("the breed emitted a component neither preset names");

        if (strayed)
            fail("a gene travelled outside the corridor the presets "
                 "declared");

        if (!sawHum)
            fail("a component only one preset names was never emitted");

        if (humMoved)
            fail("a component with nowhere to travel moved anyway");
    }

    std::filesystem::remove(tmp);
}

/* ---- 6b. a picture that is also a control ------------------------------ */

/* composer_input, and what it costs.
 *
 * `gen::life' is the first module whose draw is touchable, and the
 * property worth pinning is the one the canvas cannot show: a click goes
 * to the plugin's own state, changes what is played from the next
 * generation on, and does *not* change the file. Everything else here
 * follows from that -- a board nobody clicked replays from the piece
 * alone, and a board somebody clicked replays given the same clicks,
 * which is the boundary live MIDI already has.
 *
 * The clicks are scripted rather than real, for the same reason a learned
 * composer is driven by its params: a gate that needed a mouse would not
 * be a gate. What is driven is the ABI, not the widget -- composercheck is
 * where the widget gets pressed.
 */
static void
checkInput (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth)
{
    std::map<std::string, thcPlugin *>::const_iterator it =
        plugins.find("life");

    if (it == plugins.end())
    {
        fail("the 'life' module is missing; build the plugins first");
        return;
    }

    thcPlugin *life = it->second;

    if (!life->hasInput())
        fail("gen::life does not export composer_input");

    if (!life->hasCapture())
        fail("gen::life does not export composer_capture");

    const std::string tmp = thUtil::tempFile("gencheck-life-");

    if (tmp.empty())
    {
        fail("could not make a life scratch file");
        return;
    }

    /* A blinker: three in a row, which oscillates with period two. Small
       enough that every assertion below can be reasoned about by hand. */
    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "chain c {\n"
            "    stage b gen::life {\n"
            "        board = \"...../.OOO./...../...../.....\";\n"
            "        width = 5; height = 5;\n"
            "        scatter = 0; trigger = 1; wrap = 1;\n"
            "        notes = \"C4 D4 E4 F4 G4\";\n"
            "        period = 0.5 s; hold = 0.2 s; vel = 90;\n"
            "    };\n"
            "    sink { channel = 1; };\n"
            "};\n";
    }

    /* A click, in the coordinate space composer_draw is given. The board
       is square and centred, so a cell's middle is arithmetic the test
       can do as well as the plugin can -- deliberately, because a test
       that asked the plugin where its cells were would be checking the
       plugin against itself. */
    struct Clicker {
        thcPlugin *plugin;
        void      *state;
        double     w, h;

        void at (int col, int row, thcInputType type) const
        {
            at(col, row, type, 1);
        }

        void at (int col, int row, thcInputType type, int button) const
        {
            const double cell = (w / 5 < h / 5) ? w / 5 : h / 5;
            const double ox = (w - cell * 5) / 2;
            const double oy = (h - cell * 5) / 2;

            thcInputEvent ev;

            ev.type = type;
            ev.x = ox + (col + 0.5) * cell;
            ev.y = oy + (row + 0.5) * cell;
            ev.w = w;
            ev.h = h;
            ev.button = button;

            plugin->input(state, &ev);
        }
    };

    /* --- untouched, the piece replays from itself --- */
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n",
                        loader.errors()[i].c_str());

            fail("the life piece did not load");
            std::filesystem::remove(tmp);
            return;
        }

        const std::string first = render(sched, 6.0, 0.02);

        sched.reset();

        const std::string second = render(sched, 6.0, 0.02);

        if (first.empty())
            fail("gen::life delivered nothing");

        if (first != second)
            fail("gen::life replayed differently with nobody touching it");
    }

    /* --- the same clicks give the same music --- */
    std::string tapes[2];

    for (int pass = 0; pass < 2; pass++)
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
        {
            fail("the life piece did not load on pass two");
            std::filesystem::remove(tmp);
            return;
        }

        thcChain *c = sched.chain(0);

        if (c == NULL || c->stages.empty())
        {
            fail("the life piece has no stage to click");
            std::filesystem::remove(tmp);
            return;
        }

        thcStage *st = c->stages[0].get();
        Clicker click = { st->plugin, st->state, 100.0, 100.0 };

        /* Kill one end of the blinker and add a cell elsewhere. Pressed
           and released, because that is the pair the canvas sends and a
           plugin is entitled to keep state between them. */
        click.at(1, 1, THC_IN_PRESS);
        click.at(1, 1, THC_IN_RELEASE);
        click.at(4, 4, THC_IN_PRESS);
        click.at(4, 4, THC_IN_RELEASE);

        tapes[pass] = render(sched, 6.0, 0.02);
    }

    if (tapes[0].empty())
        fail("a clicked board delivered nothing");

    if (tapes[0] != tapes[1])
        fail("the same clicks gave a different piece -- input is the "
             "only thing that changed");

    /* --- and the clicks actually did something --- */
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        loader.load(tmp, &sched);

        const std::string untouched = render(sched, 6.0, 0.02);

        if (untouched == tapes[0])
            fail("clicking the board changed nothing about what it plays");
    }

    /* --- capture hands back what was clicked, as text --- */
    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        loader.load(tmp, &sched);

        thcChain *c = sched.chain(0);
        thcStage *st = c->stages[0].get();

        const int idx = st->plugin->paramIndex("board");

        if (idx < 0)
            fail("gen::life has no 'board' param to capture");
        else
        {
            const std::string before = st->plugin->capture(st->state, idx);

            Clicker click = { st->plugin, st->state, 100.0, 100.0 };

            click.at(0, 0, THC_IN_PRESS);
            click.at(0, 0, THC_IN_RELEASE);

            const std::string after = st->plugin->capture(st->state, idx);

            if (before.empty() || after.empty())
                fail("capture returned nothing for the board param");
            else if (before == after)
                fail("capture did not see the click");
            else if (after.find('O') == std::string::npos ||
                     after.find('.') == std::string::npos ||
                     after.find('/') == std::string::npos)
                fail("captured board is not in the format the file "
                     "writes: " + after);

            /* The round trip, which is the whole reason a board is a
               string: capture, write it back the way the host does, and
               capture again. Anything but equality means the writer and
               the reader of this format disagree, and a piece saved
               through the panel would come back as a different board. */
            st->params.setString(idx, after);

            if (st->plugin->capture(st->state, idx) != after)
                fail("a captured board did not survive a round trip "
                     "through the param");

            /* ...but a board stated in the file or typed in the panel
               outranks a click, or a pattern nobody could correct would
               be one click away. */
            st->params.setString(idx, "OOOOO/...../...../...../.....");

            const std::string typed = st->plugin->capture(st->state, idx);

            if (typed.compare(0, 5, "OOOOO") != 0)
                fail("a board written after a click was ignored: " + typed);

            /* The secondary button erases rather than toggling, which
               is the whole reason thcInputEvent carries a button. Two
               right-clicks on one cell must leave it dead; two left
               clicks put it back where it started. */
            st->params.setString(idx, "OOOOO/OOOOO/OOOOO/OOOOO/OOOOO");

            Clicker erase = { st->plugin, st->state, 100.0, 100.0 };

            erase.at(2, 2, THC_IN_PRESS, 3);
            erase.at(2, 2, THC_IN_RELEASE, 3);

            const std::string once = st->plugin->capture(st->state, idx);

            erase.at(2, 2, THC_IN_PRESS, 3);
            erase.at(2, 2, THC_IN_RELEASE, 3);

            const std::string twice = st->plugin->capture(st->state, idx);

            if (once == "OOOOO/OOOOO/OOOOO/OOOOO/OOOOO")
                fail("a secondary click did not erase a cell");
            else if (once != twice)
                fail("a secondary click toggled rather than erasing");

            erase.at(2, 2, THC_IN_PRESS, 1);
            erase.at(2, 2, THC_IN_RELEASE, 1);

            if (st->plugin->capture(st->state, idx) != 
                "OOOOO/OOOOO/OOOOO/OOOOO/OOOOO")
                fail("the primary button did not put the cell back");

            /* A param the plugin cannot capture says so, rather than
               handing back something a host would then write. */
            const int other = st->plugin->paramIndex("period");

            if (other >= 0 && !st->plugin->capture(st->state, other).empty())
                fail("capture answered for a param it has nothing to say "
                     "about");
        }
    }

    std::filesystem::remove(tmp);
}

/* ---- 6b2. the grid, driven a step at a time ---------------------------- */

/* `gen::grid' is the one composer a person edits while it plays, and the
 * three things that makes true are all invisible to a note tape.
 *
 * WHERE A PLAYED NOTE LANDS. With `listen = 1' the grid is a recorder:
 * an arriving note lights the cell it names, the ladder run backwards
 * for the row and the playhead run backwards for the column. The column
 * is the part that can be wrong by one and still look right -- the
 * pattern is there, it is a pattern, it loops, and every phrase anybody
 * recorded sat a step to the right of where they played it. So the
 * receive is driven here directly, at times this test chooses, and both
 * ways round: a note that arrives before the tick of the step it belongs
 * to and one that arrives after must land in the same cell.
 *
 * WHAT A GESTURE IS. The canvas sends a press, some drags and a release;
 * the plugin decides what they meant. A click on a note is a cycle and a
 * drag from one is a length, and telling them apart is the pointer
 * having left the cell -- not having moved at all, which every pointer
 * does. And a drag whose press went somewhere else is not this stage's
 * gesture, which the desktop canvas enforces on its side and the page's
 * tracks did not.
 *
 * WHERE THE LADDER STOPS. A grid taller than its scale climbs in
 * octaves, and `rows = 32' over a short one climbs off the end of MIDI.
 *
 * Driven through the ABI rather than through the scheduler on purpose:
 * what is being pinned is the order tick and receive happen in, and a
 * test that let the scheduler choose would be pinning the scheduler.
 */
static void
checkGrid (const std::map<std::string, thcPlugin *> &plugins,
           thSynth *synth)
{
    std::map<std::string, thcPlugin *>::const_iterator it =
        plugins.find("grid");

    if (it == plugins.end())
    {
        fail("the 'grid' module is missing; build the plugins first");
        return;
    }

    if (!it->second->hasInput() || !it->second->hasCapture() ||
        !it->second->hasReceive())
        fail("gen::grid does not export input, capture and receive");

    /* What a tick emitted. The scheduler's sink does a great deal more
       than this and none of it is what is under test. */
    struct Heard {
        std::vector<int> notes;

        static void emit (void *ctx, const thcEvent *ev)
        {
            if (ev->type == THC_EV_NOTE)
                static_cast<Heard *>(ctx)->notes.push_back(ev->u.note.note);
        }
    };

    /* One grid, loaded from text the way a piece would state it, with
       the stage handed back to be driven by hand. */
    struct Rig {
        thcScheduler sched;
        thcGenLoader loader;
        std::string  path;
        thcStage    *stage;
        int          cellsIdx;

        Rig (thSynth *synth, const std::map<std::string, thcPlugin *> &p)
            : sched(synth), loader(p), stage(NULL), cellsIdx(-1) {}

        ~Rig ()
        {
            if (!path.empty())
                std::filesystem::remove(path);
        }

        bool build (const std::string &body)
        {
            path = thUtil::tempFile("gencheck-grid-");

            if (path.empty())
                return false;

            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "chain c {\n" << body << "    sink { channel = 1; };\n"
                    << "};\n";
            }

            if (!loader.load(path, &sched))
                return false;

            thcChain *c = sched.chain(0);

            if (c == NULL || c->stages.empty())
                return false;

            stage = c->stages[0].get();
            cellsIdx = stage->plugin->paramIndex("cells");

            return cellsIdx >= 0;
        }

        /* The step at `now', played. The return is the sleep, which
           nothing here needs: the caller says when the next step is. */
        void tick (double now, Heard *heard)
        {
            thcTransport t = { now, 120.0, now * 2, 1 };
            thcEventSink sink = { heard, Heard::emit };

            stage->plugin->tick(stage->state, &t, &sink);
        }

        /* A note played into it, at a transport time of its own. */
        void play (double at, int note, Heard *heard)
        {
            thcEvent ev;
            thcEventSink sink = { heard, Heard::emit };

            memset(&ev, 0, sizeof(ev));

            ev.type = THC_EV_NOTE;
            ev.at = at;
            ev.channel = 0;
            ev.u.note.note = note;
            ev.u.note.velocity = 90;
            ev.u.note.duration = 0.2;

            stage->plugin->receive(stage->state, &ev, &sink);
        }

        std::string cells ()
        {
            return stage->plugin->capture(stage->state, cellsIdx);
        }
    };

    /* A gesture, in the coordinates composer_draw is given. The grid
       fills the whole area it is handed -- cells are not square, which
       is the difference between this and a Life board -- so a cell's
       middle is arithmetic this test can do without asking the plugin
       where it put anything. */
    struct Gesture {
        thcStage *stage;
        int       steps, rows;
        double    w, h;

        void at (int col, int row, thcInputType type, int button = 1) const
        {
            thcInputEvent ev;

            ev.type = type;
            ev.x = (col + 0.5) * (w / steps);
            ev.y = (row + 0.5) * (h / rows);
            ev.w = w;
            ev.h = h;
            ev.button = button;

            stage->plugin->input(stage->state, &ev);
        }
    };

    /* ---- a played note lands in the step it was played in ---- */

    /* Eight steps of a second each on a one-note ladder, so the only
       thing a capture can say is *which column*. Notes at 0, 2, 4 and 6
       seconds are steps 0, 2, 4 and 6, and the answer is the same
       whichever side of the step's tick they arrive on -- a scheduler is
       entitled to either order and both used to give a different
       pattern, one of them a step out.
     *
     * `pass = 0' because nothing downstream is listening, and
     * `vel`/`hold' are left at their defaults: what is being read is the
     * cells param, not a tape. */
    const char *recorder =
        "    stage g gen::grid {\n"
        "        cells = \"........\";\n"
        "        steps = 8; rows = 1; notes = \"C4\";\n"
        "        period = 1 s; hold = 0.5 s; listen = 1; pass = 0;\n"
        "    };\n";

    for (int order = 0; order < 2; order++)
    {
        Rig rig(synth, plugins);

        if (!rig.build(recorder))
        {
            fail("the grid recorder piece did not load");
            return;
        }

        Heard heard;

        for (int step = 0; step < 8; step++)
        {
            const double now = step;

            if (order == 0)
            {
                rig.tick(now, &heard);

                if (step % 2 == 0)
                    rig.play(now, 60, &heard);
            }
            else
            {
                if (step % 2 == 0)
                    rig.play(now, 60, &heard);

                rig.tick(now, &heard);
            }
        }

        const std::string drew = rig.cells();
        const char *when = order == 0 ? "after the step's tick"
                                      : "before the step's tick";

        if (drew != "x.x.x.x.")
            fail(std::string("a phrase played into gen::grid ") + when +
                 " was recorded in the wrong steps: " + drew +
                 ", wanted x.x.x.x.");
    }

    /* And a pitch the ladder cannot spell draws nothing, rather than
       landing on the nearest row it can find -- the rule gen::life
       follows and for the same reason. */
    {
        Rig rig(synth, plugins);

        if (!rig.build(recorder))
        {
            fail("the grid recorder piece did not load for C#4");
            return;
        }

        Heard heard;

        rig.tick(0, &heard);
        rig.play(0, 61, &heard);

        if (rig.cells() != "........")
            fail("gen::grid drew a cell for a pitch its ladder cannot "
                 "spell: " + rig.cells());
    }

    /* ---- a click on a tied note keeps its tail ---- */

    /* A note four steps long, clicked once. The click is a press, a
     * drag that never leaves the cell it was pressed in, and a release
     * -- which is what a real pointer sends, because a hand holding a
     * button still moves a pixel or two.
     *
     * That drag is not a length. Treating it as one set the note back to
     * a single step, so clicking a held note to accent it rubbed out the
     * tail -- and only when the pointer happened to twitch, which is the
     * kind of bug that gets reported as "sometimes".
     */
    {
        Rig rig(synth, plugins);

        const char *held =
            "    stage g gen::grid {\n"
            "        cells = \"x---....\";\n"
            "        steps = 8; rows = 1; notes = \"C4\";\n"
            "        period = 1 s; listen = 0;\n"
            "    };\n";

        if (!rig.build(held))
        {
            fail("the tied-note grid piece did not load");
            return;
        }

        Gesture g = { rig.stage, 8, 1, 800.0, 100.0 };

        g.at(0, 0, THC_IN_PRESS);
        g.at(0, 0, THC_IN_DRAG);
        g.at(0, 0, THC_IN_RELEASE);

        if (rig.cells() != "X---....")
            fail("a click on a tied note did not accent it and keep its "
                 "tail: " + rig.cells());

        /* A drag that does leave is a length, and coming back to the
           note is a shortening -- which is the edit the twitch above
           must not be mistaken for. */
        g.at(0, 0, THC_IN_PRESS);
        g.at(2, 0, THC_IN_DRAG);
        g.at(0, 0, THC_IN_DRAG);
        g.at(0, 0, THC_IN_RELEASE);

        if (rig.cells() != "X.......")
            fail("a drag out from a note and back did not shorten it: " +
                 rig.cells());
    }

    /* ---- a drag no press here began is not an edit ---- */

    /* The composer canvas feeds the plugin only between a press it took
       and the release that ends it. Nothing made that a property of the
       plugin, and a second sender arrived -- the page's sequencer sends
       a gesture per track -- that had a drag begun on one track crossing
       the next one and painting it. */
    {
        Rig rig(synth, plugins);

        const char *empty =
            "    stage g gen::grid {\n"
            "        cells = \"........\";\n"
            "        steps = 8; rows = 1; notes = \"C4\";\n"
            "        period = 1 s; listen = 0;\n"
            "    };\n";

        if (!rig.build(empty))
        {
            fail("the empty grid piece did not load");
            return;
        }

        Gesture g = { rig.stage, 8, 1, 800.0, 100.0 };

        /* A real gesture first, and it has to be a painting one: what a
           stray drag writes is whatever the last gesture was writing, so
           a grid that had never been drawn on would have it writing
           empty over empty and this would pass either way. */
        g.at(1, 0, THC_IN_PRESS);
        g.at(2, 0, THC_IN_DRAG);
        g.at(2, 0, THC_IN_RELEASE);

        if (rig.cells() != ".xx.....")
            fail("a drag across empty cells did not draw a run: " +
                 rig.cells());

        g.at(5, 0, THC_IN_DRAG);
        g.at(6, 0, THC_IN_DRAG);
        g.at(6, 0, THC_IN_RELEASE);

        if (rig.cells() != ".xx.....")
            fail("a drag gen::grid saw no press for painted cells: " +
                 rig.cells());

        /* And the release above left nothing behind: the press after it
           is an ordinary one. */
        g.at(4, 0, THC_IN_PRESS);
        g.at(4, 0, THC_IN_RELEASE);

        if (rig.cells() != ".xx.x...")
            fail("a press after a stray drag did not put a note down: " +
                 rig.cells());

        /* A gesture that ended outside the picture ended.
         *
         * A drag holds a pointer capture, so its coordinates leave the
         * canvas as soon as the hand does, and the release often lands
         * out there too. Measuring the cell first and giving up when
         * there is not one left the gesture standing -- and the next
         * stray drag across this stage went on painting, which is the
         * hole the gate above is supposed to have closed. */
        thcInputEvent away;

        away.w = 800.0;
        away.h = 100.0;
        away.x = 900.0;                  /* past the right-hand edge    */
        away.y = 50.0;
        away.button = 1;

        g.at(0, 0, THC_IN_PRESS);

        away.type = THC_IN_DRAG;
        rig.stage->plugin->input(rig.stage->state, &away);

        away.type = THC_IN_RELEASE;
        rig.stage->plugin->input(rig.stage->state, &away);

        g.at(7, 0, THC_IN_DRAG);

        if (rig.cells() != "xxx.x...")
            fail("a drag after a gesture that was released off the "
                 "picture went on painting: " + rig.cells());
    }

    /* ---- a resize keeps what somebody drew ---- */

    /* `rows' is a param, and it moves on its own: the page sets it to 1
     * when the instrument under a track turns out to ignore the note it
     * is sent, and back when it does not. That is a reshaping of the
     * picture and must not be a discarding of it -- reparsing on every
     * size change meant a grid taken to one row and back came back as
     * the pattern the *file* shipped, throwing away everything clicked
     * since.
     *
     * What fits is kept, from the bottom-left: row 0 is the lowest note
     * and a shorter ladder is the bottom of the one that was there.
     */
    {
        Rig rig(synth, plugins);

        const char *four =
            "    stage g gen::grid {\n"
            "        cells = \"..../..../..../....\";\n"
            "        steps = 4; rows = 4; notes = \"C4\";\n"
            "        period = 1 s; listen = 0;\n"
            "    };\n";

        if (!rig.build(four))
        {
            fail("the resizable grid piece did not load");
            return;
        }

        const int rowsIdx = rig.stage->plugin->paramIndex("rows");

        if (rowsIdx < 0)
        {
            fail("gen::grid has no 'rows' param to set");
            return;
        }

        Gesture g = { rig.stage, 4, 4, 400.0, 400.0 };

        /* A note on the bottom row and one on the top, drawn rather than
           stated: the file's pattern is empty, so anything that survives
           below survives because it was kept and not because it was
           reparsed. Rows are drawn top-down, so screen row 3 is the
           bottom of the ladder. */
        g.at(0, 3, THC_IN_PRESS);
        g.at(0, 3, THC_IN_RELEASE);
        g.at(2, 0, THC_IN_PRESS);
        g.at(2, 0, THC_IN_RELEASE);

        if (rig.cells() != "..x./..../..../x...")
            fail("the two clicks did not land where the test put them: " +
                 rig.cells());

        /* Down to one row. The bottom of the ladder is what is left. */
        rig.stage->params.set(rowsIdx, 1);
        rig.stage->plugin->paramChanged(rig.stage->state, rowsIdx);

        if (rig.cells() != "x...")
            fail("a grid taken to one row did not keep the bottom row "
                 "somebody drew: " + rig.cells());

        /* And back up. The row that was kept is still there and the ones
           that were not are empty -- not the file's pattern, which is
           what reparsing would have given back. */
        rig.stage->params.set(rowsIdx, 4);
        rig.stage->plugin->paramChanged(rig.stage->state, rowsIdx);

        if (rig.cells() != "..../..../..../x...")
            fail("a grid taken back up did not keep what fitted: " +
                 rig.cells());
    }

    /* But a pattern nobody touched is the file's, at whatever size: a
     * resize is not an edit, and there is nothing to preserve that
     * reparsing would not give back.
     *
     * And the two paths drop different rows, which is not an oversight.
     * Reading a text shorter than the grid is parseCells' rule -- the
     * written rows are taken in order and the ones past the end are
     * dropped, so a four-row pattern read into two rows is its first two
     * written rows, which are its top two degrees. Keeping a drawn
     * pattern is a different question: a cell is at a pitch, `rows' has
     * moved under it, and the cells that still have a row are the ones
     * whose pitch the shorter ladder still reaches -- the bottom.
     */
    {
        Rig rig(synth, plugins);

        const char *stated =
            "    stage g gen::grid {\n"
            "        cells = \"x.x./..../..../.x.x\";\n"
            "        steps = 4; rows = 4; notes = \"C4\";\n"
            "        period = 1 s; listen = 0;\n"
            "    };\n";

        if (!rig.build(stated))
        {
            fail("the stated grid piece did not load");
            return;
        }

        const int rowsIdx = rig.stage->plugin->paramIndex("rows");

        rig.stage->params.set(rowsIdx, 2);
        rig.stage->plugin->paramChanged(rig.stage->state, rowsIdx);

        if (rig.cells() != "x.x./....")
            fail("a resized grid nobody drew on did not come from its "
                 "own text: " + rig.cells());
    }

    /* ---- the ladder stops where MIDI does ---- */

    /* Eight rows over a one-note ladder is eight octaves: rows 0 to 5
     * are 60 to 120 and rows 6 and 7 are 132 and 144, which are not
     * notes. A row past the top is silent, the answer gen::ca and
     * gen::lsystem give; the alternative is what this had, which was an
     * int cast into a MIDI note number and a synth asked for a pitch
     * four octaves above the top of a piano.
     *
     * Every row hit on the first step, so one tick asks the whole
     * question.
     */
    {
        Rig rig(synth, plugins);

        const char *tall =
            "    stage g gen::grid {\n"
            "        cells = \"x.../x.../x.../x.../x.../x.../x.../x...\";\n"
            "        steps = 4; rows = 8; notes = \"C4\";\n"
            "        period = 1 s; listen = 0;\n"
            "    };\n";

        if (!rig.build(tall))
        {
            fail("the tall grid piece did not load");
            return;
        }

        Heard heard;

        rig.tick(0, &heard);

        if (heard.notes.size() != 6)
            fail("a grid eight octaves tall did not play the six rows "
                 "that are notes: " + std::to_string(heard.notes.size()));

        for (size_t i = 0; i < heard.notes.size(); i++)
            if (heard.notes[i] < 0 || heard.notes[i] > 127)
            {
                fail("gen::grid played a pitch that is not MIDI: " +
                     std::to_string(heard.notes[i]));
                break;
            }
    }
}

/* ---- 6c. two things the window asks the scheduler ---------------------- */

/* Does the tempo mean anything to this piece, and can a dead automaton
 * be brought back?
 *
 * Both are here rather than in composercheck because both are questions
 * about the *engine*, and the window only asks them. The tempo control
 * was offered on every piece and did nothing on nine of eleven, because
 * it scales beat-valued durations and most pieces are written in
 * seconds -- and nudging it wrote a `tempo' line into a file that never
 * had one. An empty CA ring is a fixed point for every rule that maps
 * 000 to 0, so the knob that emptied it cannot refill it.
 */
static void
checkTempoAndRevival (const std::map<std::string, thcPlugin *> &plugins,
                      thSynth *synth)
{
    const std::string tmp = thUtil::tempFile("gencheck-beats-");

    if (tmp.empty())
    {
        fail("could not make a beats scratch file");
        return;
    }

    /* Seconds only: the tempo has nothing to scale. */
    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out << "chain c { stage s gen::eno_line { period = 2 s; };"
               " sink { channel = 1; }; };\n";
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
            fail("the seconds-only piece did not load");
        else if (sched.usesBeats())
            fail("a piece written in seconds claimed the tempo moves it");
    }

    /* The same piece with one duration in beats. */
    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out << "tempo 90;\n"
               "chain c { stage s gen::eno_line { period = 2 beats; };"
               " sink { channel = 1; }; };\n";
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(tmp, &sched))
            fail("the beats piece did not load");
        else if (!sched.usesBeats())
            fail("a piece written in beats claimed the tempo does not "
                 "move it");
    }

    /* --- a CA emptied by its rule, and clicked back --- */
    if (plugins.find("ca") == plugins.end())
    {
        fail("the 'ca' module is missing; build the plugins first");
        std::filesystem::remove(tmp);
        return;
    }

    {
        /* Rule 0 sends every neighbourhood to 0, so the ring is empty
           after one row and stays empty however the rule is changed --
           which is exactly the state a knob can reach and not leave. */
        std::ofstream out(tmp.c_str(), std::ios::trunc);

        out <<
            "chain c {\n"
            "    stage s gen::ca {\n"
            "        rule = 0; width = 8; scatter = 0; trigger = 1;\n"
            "        notes = \"C4 D4 E4 G4\";\n"
            "        period = 0.25 s; hold = 0.2 s; vel = 90;\n"
            "    };\n"
            "    sink { channel = 1; };\n"
            "};\n";
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(tmp, &sched))
    {
        fail("the dead-ca piece did not load");
        std::filesystem::remove(tmp);
        return;
    }

    thcChain *c = sched.chain(0);

    if (c == NULL || c->stages.empty())
    {
        fail("the dead-ca piece has no stage");
        std::filesystem::remove(tmp);
        return;
    }

    thcStage *st = c->stages[0].get();

    if (!st->plugin->hasInput())
        fail("gen::ca does not export composer_input");

    /* render() runs the transport *to* a time rather than for one, so
       each leg names a later mark than the last. Passing the same bound
       twice renders nothing at all, silently, which is a way to write a
       test that always passes. */

    render(sched, 4.0, 0.02);                    /* run it dead         */

    const std::string silent = render(sched, 8.0, 0.02);

    if (!silent.empty())
        fail("rule 0 did not empty the ring");

    /* Turning the knob does not help, which is the trap.
     *
       The index is checked before it is used. It cannot be -1 today --
       the piece above declares `rule' and the loader accepted it -- but
       paramIndex answers -1 for a name the plugin does not have, and a
       -1 handed to params.set is an out-of-range write that a renamed
       param would introduce silently. The rest of this file guards its
       lookups; this one had been the exception. */
    const int ruleIdx = st->plugin->paramIndex("rule");

    if (ruleIdx < 0)
    {
        fail("gen::ca has no `rule' param to turn");
        std::filesystem::remove(tmp);
        return;
    }

    st->params.set(ruleIdx, 110);

    if (!render(sched, 12.0, 0.02).empty())
        fail("an empty ring came back from a rule change alone -- the "
             "trap this is about does not exist");

    /* A click in the history does not, which is the other half of the
       bargain: the rows above the present one are what happened, and
       what happened is not editable. Checked before the click that
       works, so that "a click revived it" cannot be satisfied by a
       plugin that takes every click anywhere. */
    {
        thcInputEvent ev;

        ev.type = THC_IN_PRESS;
        ev.x = 50;
        ev.y = 50;                  /* halfway up: history              */
        ev.w = 100;
        ev.h = 100;
        ev.button = 1;

        st->plugin->input(st->state, &ev);
    }

    if (!render(sched, 14.0, 0.02).empty())
        fail("a click in the history edited the present row");

    /* A click does. The present row is the bottom band of the draw, and
       the coordinates are the ones composer_draw is given. */
    {
        thcInputEvent ev;

        ev.type = THC_IN_PRESS;
        ev.x = 50;                  /* somewhere along the ring          */
        ev.y = 99;                  /* the bottom row's band             */
        ev.w = 100;
        ev.h = 100;
        ev.button = 1;

        st->plugin->input(st->state, &ev);
    }

    if (render(sched, 18.0, 0.02).empty())
        fail("a clicked cell did not bring the automaton back");

    std::filesystem::remove(tmp);
}

/* ---- 6d. instruments: a piece that carries what it is played on -------- */

/* A piece that carries what it is played on. Four claims, each of which
 * fails silently if nothing watches it:
 *
 * 1. The block parses and the graph actually arrives on a channel. This
 *    is the one that needs a real thSynth with a real plugin path, which
 *    is why main() builds one -- an instrument that "loaded" because
 *    nothing tried is not a test.
 * 2. Channels are allocated in declaration order, lowest free first,
 *    around whatever `channel = N' claimed. That assignment is stable
 *    across loads and a great deal keys off it.
 * 3. Sinks bound by name get the number, so the events go somewhere.
 * 4. The rejections, by name and line. The unit rules especially: a
 *    chanarg written in the wrong unit is a value silently a thousand
 *    times wrong, which is the failure this format exists to refuse.
 */
/* Every channel empty, and the queue that empties them drained.
 *
 * The checks below ask what is on a channel, and a scheduler going out
 * of scope does not unload what its piece loaded -- a patch outlives the
 * file that asked for it, deliberately. So the sub-tests would be
 * answering with the previous one's leftovers. Nothing else in this
 * harness cares, because the harness installs no channelTaken hook and
 * allocation therefore ignores what is loaded. */
static void
clearChannels (thSynth *synth)
{
    for (int i = 0; i < TH_MIDI_CHANNELS; i++)
        synth->removeChan(i);

    drainSynth();
}

/* A scratch .dsp declaring `name' with no unit at all.
 *
 * amb01 is used throughout above because it is a real instrument with
 * real chanargs; what it cannot be is a *second* graph declaring one of
 * those names differently, and that shape is the one a knob binding has
 * to survive being swapped onto. So it is written here, the way argtype
 * writes the .dsp files whose cases the corpus cannot contain. Built
 * from amb01 rather than from nothing, so it stays a graph that loads
 * and sounds; only the one declaration is rewritten. Returns the path,
 * or "" if it could not be written. */
static std::string
writeUnitlessArg (const std::string &name)
{
    const std::string src =
        thUtil::findDataFile("amb01.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);

    if (src.empty())
        return "";

    std::ifstream in(src.c_str());
    std::string line, out;

    if (!in)
        return "";

    /* `@r = 100 ms;' -> `@r = 0.5;', and the same for its .max, which
       would otherwise put the unit back: a .dsp takes the unit from
       whichever value site states one first. */
    while (std::getline(in, line))
    {
        const std::string decl = "@" + name + " = ";
        const std::string maxd = "@" + name + ".max = ";
        size_t at = line.find(decl);

        if (at != std::string::npos)
            line = line.substr(0, at) + decl + "0.5;";
        else if ((at = line.find(maxd)) != std::string::npos)
            line = line.substr(0, at) + maxd + "1;";

        out += line + "\n";
    }

    std::string path = thUtil::tempFile("gencheck-plain-");

    if (path.empty())
        return "";

    {
        std::ofstream o(path.c_str(), std::ios::trunc);

        o << out;

        if (!o.good())
        {
            std::filesystem::remove(path);
            return "";
        }
    }

    return path;
}

/* The second graph an instrument can name: the one that runs on the sum
 * of its voices rather than the one that makes them.
 *
 * What is worth holding down here is the seam, not the effect -- fxcheck
 * covers what a thChanEffect does. This is that the clause reaches it: the
 * file is found, the values land in the effect's chanarg map and not the
 * instrument's, and the two are addressed apart.
 */
static void
checkInstrumentEffects (const std::map<std::string, thcPlugin *> &plugins,
                        thSynth *synth)
{
    clearChannels(synth);

    const std::string body =
        "instrument lead {\n"
        "    dsp \"amb01.dsp\";\n"
        "    a = 900 ms;\n"
        "    effect \"fx/echo.dsp\" {\n"
        "        delay = 250 ms;\n"
        "        mix = 0.5;\n"
        "    };\n"
        "};\n"
        "chain a { stage s gen::eno_line { };"
        " sink { instrument = lead; }; };\n";

    std::string path = thUtil::tempFile("gencheck-fx-");

    if (path.empty())
    {
        fail("could not make a scratch file for the effect check");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        drainSynth();

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail("a piece whose instrument carries an effect did not load");
        }
        else
        {
            thArg *delay = synth->getChanArg(0, "fx.delay");
            thArg *mix = synth->getChanArg(0, "fx.mix");
            thArg *a = synth->getChanArg(0, "a");

            if (synth->getEffect(0) == NULL)
                fail("the effect did not reach the channel");
            else if (delay == NULL || mix == NULL)
                fail("the effect's chanargs are not reachable under `fx.'");
            else if (a == NULL)
                fail("the instrument's own chanargs went with them");
            else
            {
                /* Folded at the rate the synth was built with, the way
                   every other duration in this block is. */
                const float want =
                    (float)(250.0 * synth->getSampleRate() / 1000.0);

                if (fabs((*delay)[0] - want) > 1.0)
                    fail("the effect's 250 ms did not fold at the synth's "
                         "rate");

                if (fabs((*mix)[0] - 0.5) > 1e-6)
                    fail("the effect's plain number did not land");

                /* Two maps, not one merged: the effect declares `delay' and
                   the instrument does not, so an unprefixed `delay' has to
                   find nothing. */
                if (synth->getChanArg(0, "delay") != NULL)
                    fail("the effect's chanargs leaked into the "
                         "instrument's map");

                const float wantA =
                    (float)(900.0 * synth->getSampleRate() / 1000.0);

                if (fabs((*a)[0] - wantA) > 1.0)
                    fail("the instrument's own value did not survive its "
                         "effect");
            }
        }
    }

    std::filesystem::remove(path);

    clearChannels(synth);

    /* ---- and what the clause refuses ---------------------------------- */

    expectReject(plugins, synth, "two-effects",
        "instrument i { dsp \"amb01.dsp\"; effect \"fx/echo.dsp\";"
        " effect \"fx/echo.dsp\"; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "names two effects");

    expectReject(plugins, synth, "effect-no-file",
        "instrument i { dsp \"amb01.dsp\"; effect; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "effect wants a quoted filename");

    /* The message has to name the *effect*, not the instrument's graph:
       sending the reader to amb01.dsp to look for a `nonesuch' the echo
       does not declare is one file too many. */
    expectReject(plugins, synth, "effect-bad-arg",
        "instrument i { dsp \"amb01.dsp\";"
        " effect \"fx/echo.dsp\" { nonesuch = 1; }; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "'fx/echo.dsp' declares no chanarg called 'nonesuch'");

    /* And the writer steps over the clause rather than choking on it.
     *
     * thcGenEdit indexes an instrument block statement by statement, and a
     * statement it cannot read drops the whole block out of the index -- so
     * an instrument carrying an effect would be one describe() never
     * mentioned and whose own values could not be edited. What is checked is
     * that the instrument is still there with its own value in it; the
     * effect's values are not indexed, which is the GUI half's business. */
    {
        std::string path = thUtil::tempFile("gencheck-fxedit-");

        if (path.empty())
            fail("could not make a scratch file for the effect edit check");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "instrument lead {\n"
                       "    dsp \"amb01.dsp\";\n"
                       "    a = 900 ms;\n"
                       "    effect \"fx/echo.dsp\" {\n"
                       "        delay = 375 ms;\n"
                       "    };\n"
                       "    r = 40 ms;\n"
                       "};\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = lead; }; };\n";
            }

            thcGenEdit::Doc doc;
            std::string why;

            if (thcGenEdit::describe(path, doc, why) != thcGenEdit::OK)
                fail("describe refused a piece with an effect in it: " + why);
            else if (doc.instruments.size() != 1)
                fail("an instrument carrying an effect fell out of the "
                     "index");
            else
            {
                bool sawA = false, sawR = false;

                for (size_t i = 0; i < doc.instruments[0].values.size(); i++)
                {
                    const std::string &n = doc.instruments[0].values[i].name;

                    if (n == "a") sawA = true;
                    if (n == "r") sawR = true;
                }

                if (!sawA || !sawR)
                    fail("the values on either side of an effect clause did "
                         "not both survive the scan");
            }

            std::filesystem::remove(path);
        }
    }

    expectReject(plugins, synth, "effect-missing",
        "instrument i { dsp \"amb01.dsp\";"
        " effect \"no-such-effect.dsp\"; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "did not load as an effect");

    clearChannels(synth);
}

/* `side = carrier;' inside an effect block: the other channel that effect
 * hears.
 *
 * The seam again rather than the sound -- fxcheck measures what side0
 * carries. What is held down here is the turn from a name into a number: the
 * file names an instrument, channels are allocated after the whole file has
 * been read, and what reaches the engine has to be the channel that
 * instrument landed on.
 */
static void
checkEffectSide (const std::map<std::string, thcPlugin *> &plugins,
                 thSynth *synth)
{
    clearChannels(synth);

    const std::string body =
        "instrument carrier {\n"
        "    dsp \"amb01.dsp\";\n"
        "};\n"
        "instrument voice {\n"
        "    dsp \"amb01.dsp\";\n"
        "    effect \"fx/echo.dsp\" {\n"
        "        side = carrier;\n"
        "        mix = 0.5;\n"
        "    };\n"
        "};\n"
        "chain a { stage s gen::eno_line { };"
        " sink { instrument = carrier; }; };\n"
        "chain b { stage t gen::eno_line { };"
        " sink { instrument = voice; }; };\n";

    std::string path = thUtil::tempFile("gencheck-side-");

    if (path.empty())
    {
        fail("could not make a scratch file for the side check");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        drainSynth();

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail("a piece whose effect names a side did not load");
        }
        else if (sched.instruments().size() != 2)
            fail("the side piece did not declare two instruments");
        else
        {
            const thcInstrument &carrier = sched.instruments()[0];
            const thcInstrument &voice = sched.instruments()[1];
            thChanEffect *fx = synth->getEffect(voice.channel);

            if (voice.side != "carrier")
                fail("the effect's `side' did not survive the parse");
            else if (voice.sideChannel != carrier.channel)
                fail("the side was not resolved to the carrier's channel");
            else if (fx == NULL)
                fail("the effect did not reach the channel");
            else if (fx->sideChan() != carrier.channel)
                fail("the channel the engine heard is not the one the file "
                     "named");

            /* And the carrier keeps its own effect -- which is none. A side
               is a listener, not a routing change. */
            if (synth->getEffect(carrier.channel) != NULL)
                fail("naming a channel as a side put an effect on it");
        }
    }

    std::filesystem::remove(path);

    clearChannels(synth);

    /* ---- and what the clause refuses ---------------------------------- */

    /* Declared before it is named, like a scale or a preset -- which is what
       makes a ring impossible to write here, since an instrument is not
       declared until its own block is closed. */
    expectReject(plugins, synth, "side-unknown",
        "instrument i { dsp \"amb01.dsp\";"
        " effect \"fx/echo.dsp\" { side = nobody; }; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "is not a declared instrument");

    expectReject(plugins, synth, "side-self",
        "instrument i { dsp \"amb01.dsp\";"
        " effect \"fx/echo.dsp\" { side = i; }; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "is not a declared instrument");

    expectReject(plugins, synth, "side-twice",
        "instrument a { dsp \"amb01.dsp\"; };\n"
        "instrument i { dsp \"amb01.dsp\";"
        " effect \"fx/echo.dsp\" { side = a; side = a; }; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "names two sides");

    expectReject(plugins, synth, "side-number",
        "instrument a { dsp \"amb01.dsp\"; };\n"
        "instrument i { dsp \"amb01.dsp\";"
        " effect \"fx/echo.dsp\" { side = 2; }; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = i; }; };",
        "wants the name of an instrument");

    /* The mix hears every channel already; there is no second one to name. */
    expectReject(plugins, synth, "side-on-master",
        "instrument a { dsp \"amb01.dsp\"; };\n"
        "effect \"fx/echo.dsp\" { side = a; };\n"
        "chain c { stage s gen::eno_line { }; sink { instrument = a; }; };",
        "cannot name a side");

    clearChannels(synth);
}

/* And the other half of the seam: a *sink* aimed at an effect's knob.
 *
 * Setting an effect's chanargs in the `effect' block is one thing and
 * moving one while the piece runs is another, and until this was written
 * only the first worked: every layer from thcScheduler down already
 * spoke TH_EFFECT_PREFIX -- thSynth::getChanArg splits on it, the
 * scheduler's refusal message reads it, an `effect' block's values are
 * stored behind it -- and the sink's name check took an identifier and
 * refused the dot. So docs/GEN_FORMAT.md documented `chanarg = "fx.delay"'
 * and the loader rejected the file, and a Leslie's spin-up or a filter
 * sweep on a channel effect was a thing a piece could describe and not
 * perform.
 *
 * What is held down here is the whole path in one go, because the parts
 * were each fine on their own: the file loads, the name survives to
 * delivery with its prefix intact (the tape is what the piano roll and
 * the replay gate both read), and the value lands on the *effect's*
 * `mix' rather than on an instrument chanarg of the same name.
 *
 * `mix' is deliberately a name both graphs could plausibly have. amb01
 * does not declare one, so a value arriving unprefixed would find
 * nothing and this would pass by accident -- which is why the check
 * below is that the effect's own arg moved, and not merely that
 * something did.
 */
static void
checkEffectChanargSink (const std::map<std::string, thcPlugin *> &plugins,
                        thSynth *synth)
{
    clearChannels(synth);

    const std::string body =
        "instrument lead {\n"
        "    dsp \"amb01.dsp\";\n"
        "    effect \"fx/echo.dsp\" {\n"
        "        delay = 250 ms;\n"
        "        mix = 0.1;\n"
        "    };\n"
        "};\n"
        "chain a { stage s gen::eno_line { };"
        " sink { instrument = lead; }; };\n"
        "chain m {\n"
        "    stage w gen::walk { min = 0.6; max = 0.9; step = 0.3;"
        " period = 0.25 s; };\n"
        "    sink { instrument = lead; chanarg = \"fx.mix\"; };\n"
        "};\n";

    std::string path = thUtil::tempFile("gencheck-fxsink-");

    if (path.empty())
    {
        fail("could not make a scratch file for the effect sink check");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        drainSynth();

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail("a sink aimed at `fx.mix' did not load");
        }
        else
        {
            thArg *mix = synth->getChanArg(0, "fx.mix");

            if (mix == NULL)
                fail("the effect's `mix' is not reachable under `fx.'");
            else
            {
                const std::string tape = render(sched, 4.0, 0.02);

                if (tape.find("C ") == std::string::npos)
                    fail("the walk delivered no chanarg events at all");
                else if (tape.find("fx.mix") == std::string::npos)
                    fail("a sink's `fx.' prefix was dropped before "
                         "delivery");

                /* The block set 0.1 and the walk runs 0.6 to 0.9, so
                   anything in the walk's range is the walk's doing and
                   nothing else's. */
                if ((*mix)[0] < 0.55f || (*mix)[0] > 0.95f)
                    fail("the effect's `mix' did not move with the walk: "
                         "it reads " + std::to_string((*mix)[0]));

                /* And it went to the effect rather than being invented
                   on the instrument's side of the channel. */
                if (synth->getChanArg(0, "mix") != NULL)
                    fail("an unprefixed `mix' appeared on the "
                         "instrument's map");
            }
        }
    }

    std::filesystem::remove(path);

    /* --- and the name that is not there -----------------------------
     *
     * An instrument carries two graphs, so "declares no chanarg called
     * X" has to say which of them was asked. `fx.mix' is the effect's
     * and the instrument's .dsp was never going to declare it, so a
     * message naming the .dsp sends the author to a file that cannot
     * answer -- the same split thcScheduler::writeValues makes over an
     * `effect' block's own values, and for the same reason.
     *
     * The prefix comes off the quoted name too: what has to be gone and
     * read is `mix' in the effect, and `fx.' is the engine's word for
     * where to look rather than part of anything declared anywhere. */
    expectReject(plugins, synth, "fxsink-unknown",
        "instrument lead {\n"
        "    dsp \"amb01.dsp\";\n"
        "    effect \"fx/echo.dsp\" { mix = 0.1; };\n"
        "};\n"
        "chain m { stage w gen::walk { min = 0; max = 1; step = 0.5;"
        " period = 0.25 s; };"
        " sink { instrument = lead; chanarg = \"fx.nosuch\"; }; };\n",
        "has effect 'fx/echo.dsp', which declares no chanarg called "
        "'nosuch'");

    /* And the instrument's own args keep the message they had, which is
       the half that would go quietly if the split were made the wrong
       way round. */
    expectReject(plugins, synth, "sink-unknown-instrument",
        "instrument lead {\n"
        "    dsp \"amb01.dsp\";\n"
        "    effect \"fx/echo.dsp\" { mix = 0.1; };\n"
        "};\n"
        "chain m { stage w gen::walk { min = 0; max = 1; step = 0.5;"
        " period = 0.25 s; };"
        " sink { instrument = lead; chanarg = \"nosuch\"; }; };\n",
        "is 'amb01.dsp', which declares no chanarg called 'nosuch'");

    /* --- and the writer refuses what the loader refuses --------------
     *
     * A sink onto an arg a knob already drives is refused by the loader:
     * both are pushes, so the walk wins every time it fires and the
     * slider looks dead. thcGenEdit makes the same refusal so that every
     * state it writes loads -- but it reads the instrument's values off
     * the index, and the index steps over the `effect' block. So the one
     * arg a `fx.' sink can name was the one arg the check could not see,
     * and this is the case that holds the two ends together. */
    {
        std::string bound = thUtil::tempFile("gencheck-fxknob-");

        if (bound.empty())
            fail("could not make a scratch file for the effect knob check");
        else
        {
            std::string why;

            {
                std::ofstream out(bound.c_str(), std::ios::trunc);

                out << "@w = 0.4;\n@w.min = 0;\n@w.max = 1;\n"
                       "instrument lead {\n"
                       "    dsp \"amb01.dsp\";\n"
                       "    effect \"fx/echo.dsp\" {\n"
                       "        delay = 250 ms;\n"
                       "        mix = @w;\n"
                       "    };\n"
                       "};\n"
                       "chain c {\n"
                       "    stage s gen::eno_line { };\n"
                       "    sink { instrument = lead; };\n"
                       "};\n";
            }

            /* Both writers, since addSink and setSink share the check but
               not the call site. */
            if (thcGenEdit::addSink(bound, "c", 1, "lead", "fx.mix", why) ==
                thcGenEdit::OK)
                fail("addSink wrote a sink that fights a knob on an "
                     "effect");

            if (thcGenEdit::setSink(bound, "c", 0, 1, "lead", "fx.mix",
                                    why) == thcGenEdit::OK)
                fail("setSink wrote a sink that fights a knob on an "
                     "effect");

            /* An effect arg no knob drives is still fair game, and so is
               an instrument arg of the same bare name: the two maps do
               not see each other, which is the whole reason for the
               prefix. */
            editOk(thcGenEdit::addSink(bound, "c", 1, "lead", "fx.delay",
                                       why), why,
                   "addSink onto an effect arg no knob drives");

            editOk(thcGenEdit::addSink(bound, "c", 1, "lead", "fmin", why),
                   why, "addSink onto an instrument arg beside an effect");

            /* And what it wrote loads, which is the claim the refusals
               above are in aid of. */
            {
                thcScheduler sched(synth);
                thcGenLoader loader(plugins);

                drainSynth();

                if (!loader.load(bound, &sched))
                {
                    for (size_t i = 0; i < loader.errors().size(); i++)
                        fprintf(stderr, "gencheck: %s\n",
                                loader.errors()[i].c_str());

                    fail("the file after adding effect sinks no longer "
                         "loads");
                }
            }

            std::filesystem::remove(bound);
        }
    }

    clearChannels(synth);
}

static void
checkInstruments (const std::map<std::string, thcPlugin *> &plugins,
                  thSynth *synth)
{
    clearChannels(synth);

    /* Two instruments and a sink that claimed a channel out from under
       them: `pad' takes 1, `bell' skips the claimed 2 and takes 3. */
    const std::string body =
        "instrument pad {\n"
        "    dsp \"amb01.dsp\";\n"
        "    a = 900 ms;\n"
        "    fmin = 0.2;\n"
        "};\n"
        "instrument bell {\n"
        "    dsp \"amb01.dsp\";\n"
        "    r = 40 ms;\n"
        "};\n"
        "chain other { stage s gen::eno_line { };"
        " sink { channel = 2; }; };\n"
        "chain a { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };\n"
        "chain b { stage s gen::walk { };"
        " sink { instrument = bell; chanarg = \"res\"; }; };\n";

    std::string path = thUtil::tempFile("gencheck-instr-");

    if (path.empty())
    {
        fail("could not make a scratch file for the instrument check");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        drainSynth();

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail("a piece with instruments did not load");
        }
        else
        {
            if (sched.instruments().size() != 2)
                fail("the instrument table is the wrong size");
            else
            {
                /* Engine numbering here: file 1 and 3 are 0 and 2. */
                if (sched.instruments()[0].channel != 0)
                    fail("pad did not land on the first free channel");

                if (sched.instruments()[1].channel != 2)
                    fail("bell did not skip the channel a sink claimed");

                thArg *a = synth->getChanArg(0, "a");
                thArg *f = synth->getChanArg(0, "fmin");

                if (a == NULL || f == NULL)
                    fail("the instrument's graph did not reach its channel");
                else
                {
                    /* 900 ms folded at the rate the synth was built
                       with -- not the compile-time one, which is the
                       bug thUnits exists to have fixed. */
                    const float want =
                        (float)(900.0 * synth->getSampleRate() / 1000.0);

                    if (fabs((*a)[0] - want) > 1.0)
                        fail("the attack was not folded through its unit");

                    if (fabs((*f)[0] - 0.2) > 1e-5)
                        fail("a unitless value did not arrive as written");
                }
            }

            /* The sinks got the numbers behind the names. */
            const thcChain *ca = sched.chain(1);
            const thcChain *cb = sched.chain(2);

            if (ca == NULL || ca->sinks.size() != 1 ||
                ca->sinks[0].channel != 0)
                fail("the sink bound to pad never got its channel");

            if (cb == NULL || cb->sinks.size() != 1 ||
                cb->sinks[0].channel != 2 || cb->sinks[0].chanarg != "res")
                fail("the chanarg sink bound to bell never got its channel");
        }
    }

    std::filesystem::remove(path);

    /* `%' folds too, and it is the half nothing else here touches --
       every other instrument in this file is amb01, which declares no
       percentages. A fraction of TH_MAX rather than a sample count,
       which is exactly why the diagnostic for a *missing* unit cannot
       talk about samples. */
    {
        std::string path = thUtil::tempFile("gencheck-instr-pct-");

        if (path.empty())
            fail("could not make a scratch file for the percent check");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "instrument fizz { dsp \"aspect2.dsp\"; os = 50%; };\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = fizz; }; };\n";
            }

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            clearChannels(synth);

            if (!loader.load(path, &sched))
            {
                for (size_t i = 0; i < loader.errors().size(); i++)
                    fprintf(stderr, "gencheck: %s\n",
                            loader.errors()[i].c_str());

                fail("a percentage instrument value did not load");
            }
            else
            {
                thArg *os = synth->getChanArg(0, "os");

                if (os == NULL)
                    fail("the percentage instrument did not reach its "
                         "channel");
                else if (fabs((*os)[0] - TH_MAX / 2.0) > 1.0)
                    fail("50% did not fold to half of TH_MAX");
            }

            std::filesystem::remove(path);
        }
    }

    /* ---- knobs reaching into an instrument -------------------------- */

    /* One knob, both sides of the boundary, and the whole of it. A stage
    * param bound to a knob is *read* through * it; a chanarg cannot be,
    because what reads a chanarg is the audio * graph and the only value
    it will ever see is the one in its thArg. * So this binding is a push,
    and the thing to hold down is that the * push happens -- at load, and
    again on every move, through the * unit the binding was written with.
    */ { std::string path = thUtil::tempFile("gencheck-instr-knob-");

        if (path.empty())
            fail("could not make a scratch file for the knob-binding check");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "@warmth = 0.4;\n"
                       "@warmth.min = 0;\n@warmth.max = 1;\n"
                       "@tail = 1800;\n"
                       "@tail.min = 200;\n@tail.max = 5000;\n"
                       "instrument pad {\n"
                       "    dsp \"amb01.dsp\";\n"
                       "    fmin = @warmth;\n"
                       "    r = @tail ms;\n"
                       "};\n"
                       "chain c { stage s gen::eno_line { prob = @warmth; };"
                       " sink { instrument = pad; }; };\n";
            }

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            clearChannels(synth);

            if (!loader.load(path, &sched))
            {
                for (size_t i = 0; i < loader.errors().size(); i++)
                    fprintf(stderr, "gencheck: %s\n",
                            loader.errors()[i].c_str());

                fail("a knob bound to an instrument chanarg did not load");
            }
            else
            {
                const long rate = synth->getSampleRate();
                thArg *fmin = synth->getChanArg(0, "fmin");
                thArg *r    = synth->getChanArg(0, "r");
                thArg *warmth = sched.knob("warmth");
                thArg *tail = sched.knob("tail");

                if (fmin == NULL || r == NULL || warmth == NULL ||
                    tail == NULL)
                    fail("the knob-bound instrument did not arrive whole");
                else
                {
                    /* Where the knob is, before anybody touches it: a
                       piece has to sound like its file the moment it
                       loads, not one knob-move later. */
                    if (fabs((*fmin)[0] - 0.4) > 1e-5)
                        fail("a knob-bound chanarg did not take the knob's "
                             "value at load");

                    if (fabs((*r)[0] - 1800.0 * rate / 1000.0) > 1.0)
                        fail("a knob binding with a unit was not folded "
                             "through it at load");

                    /* And on every move. */
                    warmth->setValue(0.8f);

                    if (fabs((*fmin)[0] - 0.8) > 1e-5)
                        fail("moving the knob did not move the chanarg");

                    tail->setValue(600.0f);

                    if (fabs((*r)[0] - 600.0 * rate / 1000.0) > 1.0)
                        fail("moving a unit-carrying knob did not fold "
                             "through the unit");

                    /* The same knob, still driving the composer's side.
                       That is the sentence one binding namespace is
                       about. */
                    thcChain *c = sched.chain(0);

                    if (c == NULL || c->stages.empty())
                        fail("the chain did not survive");
                    else
                    {
                        thcStage *st = c->stages[0].get();
                        const int pi = st->plugin->paramIndex("prob");

                        if (pi < 0 || fabs(st->params.get(pi) - 0.8) > 1e-5)
                            fail("one knob did not reach both worlds");
                    }

                    /* And a channel replaced from under the binding.
                     *
                     * The push looks its target up by name every time,
                     * which stops it writing through a freed pointer --
                     * and does nothing at all about that name resolving
                     * to a *different* arg. The Patch Selector replaces
                     * a channel on request, so `r' folded from ms here
                     * beside `r' running 0 to 1 on the next graph is a
                     * knob nudge writing six figures into an arg whose
                     * top is 1. Nothing in the corpus has that shape, so
                     * the .dsp is written here, the way argtype writes
                     * the ones it needs.
                     *
                     * Not hypothetical: without the fold re-check in the
                     * push, `r' below lands at 176400. */
                    const std::string plain = writeUnitlessArg("r");

                    if (plain.empty())
                        fail("could not write a scratch .dsp for the "
                             "channel-replacement check");
                    else
                    {
                        drainSynth();

                        if (synth->loadTree(plain.c_str(), 0,
                                            TH_DEFAULT_CHAN_AMP) == NULL)
                            fail("the scratch .dsp did not load");
                        else
                        {
                            drainSynth();

                            thArg *now = synth->getChanArg(0, "r");

                            if (now == NULL || !now->units().empty())
                                fail("the scratch .dsp did not give 'r' a "
                                     "different shape");
                            else
                            {
                                const float was = (*now)[0];

                                tail->setValue(4000.0f);

                                if (fabs((*now)[0] - was) > 1e-5)
                                    fail("a knob went on driving an arg "
                                         "whose fold had changed under it");
                            }
                        }

                        std::filesystem::remove(plain);
                    }
                }
            }

            std::filesystem::remove(path);
        }
    }

    /* ---- the rejections ---- */

    expectReject(plugins, synth, "instr-no-dsp",
        "instrument pad { fmin = 0.2; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "names no dsp");

    expectReject(plugins, synth, "instr-missing-file",
        "instrument pad { dsp \"no_such_graph.dsp\"; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "did not load");

    expectReject(plugins, synth, "instr-twice",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "already declared");

    expectReject(plugins, synth, "instr-unknown-arg",
        "instrument pad { dsp \"amb01.dsp\"; frobnicate = 1; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "frobnicate");

    /* The two halves of the unit rule. A duration written bare is a
       sample count nobody meant; a unit on something that has none is
       a fold that would land a thousand times off. */
    expectReject(plugins, synth, "instr-bare-duration",
        "instrument pad { dsp \"amb01.dsp\"; a = 900; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "written in ms");

    /* The other fold, and the reason that message cannot say "samples":
       a bare number on a percentage arg is a raw fraction of TH_MAX, not
       a sample count. aspect2.dsp is the one graph in the corpus that
       declares a chanarg in `%'. */
    expectReject(plugins, synth, "instr-bare-percent",
        "instrument fizz { dsp \"aspect2.dsp\"; os = 50; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = fizz; }; };",
        "written in %");

    expectReject(plugins, synth, "instr-spurious-unit",
        "instrument pad { dsp \"amb01.dsp\"; fmin = 50 ms; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "means nothing to it");

    /* A refused instrument leaves no wiring behind either.
     *
     * Bindings are connected as their values are read, so this one wires
     * `fmin' up and is then refused for an arg amb01 does not declare.
     * The graph comes off the channel; the connections have to go with
     * it, or they push into whatever is loaded onto that channel next --
     * the bug the fold re-check in the push exists for, arriving by a
     * second door. Checked by putting a graph there afterwards and
     * moving the knob, since a surviving connection still names
     * channel 0. */
    {
        const std::string dsp = thUtil::findDataFile(
            "amb01.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);

        thcScheduler sched(synth);
        thcInstrument inst;
        thcInstrumentArg a;

        clearChannels(synth);

        thArg *knob = sched.addKnob("warmth", 0.4f);

        inst.name = "pad";
        inst.dsp = "amb01.dsp";
        inst.channel = 0;

        a.name = "fmin";      a.value = 0; a.knob = "warmth";
        inst.args.push_back(a);
        a.name = "nosucharg"; a.value = 1; a.knob.clear();
        inst.args.push_back(a);

        sched.addInstrument(inst);

        std::string why;

        /* Driven through applyInstrument rather than through a .gen,
           because the loader calls clearChains on a failed load and
           that disconnects everything anyway -- which would make this
           pass whether or not the contract holds. The contract is
           stated on applyInstrument, so it is asked there. */
        if (sched.applyInstrument(0, why))
            fail("an instrument naming an arg its graph lacks applied "
                 "anyway");

        drainSynth();

        if (dsp.empty() ||
            synth->loadTree(dsp.c_str(), 0, TH_DEFAULT_CHAN_AMP) == NULL)
            fail("could not put a graph back for the wiring check");
        else
        {
            drainSynth();

            thArg *fmin = synth->getChanArg(0, "fmin");

            knob->setValue(0.9f);

            if (fmin != NULL && fabs((*fmin)[0] - 0.9) < 1e-5)
                fail("a refused instrument's knob binding survived and "
                     "drove the next patch on its channel");
        }
    }

    expectReject(plugins, synth, "instr-undeclared-knob",
        "instrument pad { dsp \"amb01.dsp\"; fmin = @nope; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "@nope");

    /* A knob binding obeys the unit rule a literal obeys, both ways
       round. The number in a knob is as unitless as the number in a
       file, so an envelope on a bare binding would be a slider running
       in samples. */
    expectReject(plugins, synth, "instr-knob-bare-duration",
        "@attack = 500;\n"
        "instrument pad { dsp \"amb01.dsp\"; a = @attack; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "written in ms");

    expectReject(plugins, synth, "instr-knob-spurious-unit",
        "@warmth = 0.5;\n"
        "instrument pad { dsp \"amb01.dsp\"; fmin = @warmth ms; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; }; };",
        "means nothing to it");

    /* A knob and a sink over one arg are both pushes, so the walk wins
       every time it fires and the slider looks dead a second after you
       let go of it. Invisible from either end, and no reading of the
       file where it was the intention. */
    expectReject(plugins, synth, "knob-and-sink-fight",
        "@warmth = 0.5;\n"
        "instrument pad { dsp \"amb01.dsp\"; fmin = @warmth; };\n"
        "chain c { stage s gen::walk { };"
        " sink { instrument = pad; chanarg = \"fmin\"; }; };",
        "would fight over it");

    expectReject(plugins, synth, "sink-unknown-instrument",
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = ghost; }; };",
        "ghost");

    expectReject(plugins, synth, "sink-both-targets",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { instrument = pad; channel = 4; }; };",
        "one or the other");

    expectReject(plugins, synth, "sink-no-target",
        "chain c { stage s gen::eno_line { }; sink { }; };",
        "no instrument and no channel");

    /* A sink bound to an instrument cannot name a knob that instrument
       does not have. Unlike a `channel = N' sink, whose patch is
       somebody else's business, this one is checkable -- and a sink
       that is not checked delivers into getChanArg's NULL forever, in
       silence, a long way from the typo. */
    expectReject(plugins, synth, "sink-unknown-chanarg",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage s gen::walk { };"
        " sink { instrument = pad; chanarg = \"nosuchknob\"; }; };",
        "nosuchknob");

    /* One bad instrument stops the rest, and takes back the one that
       already made it.
     *
     * The file is not going to load once the first fails, so every
     * instrument after it would be another graph put on another channel
     * for a piece nobody is going to hear -- and the one before it is a
     * graph on a channel for the same piece, which is the half of "a
     * file with any error loads nothing" that used to be false. */
    {
        std::string path = thUtil::tempFile("gencheck-instr-stop-");

        if (!path.empty())
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "instrument first { dsp \"amb01.dsp\"; };\n"
                       "instrument bad { dsp \"amb01.dsp\";"
                       " nosucharg = 1; };\n"
                       "instrument after { dsp \"amb01.dsp\"; };\n"
                       "chain c { stage s gen::eno_line { };"
                       " sink { instrument = bad; }; };\n";
            }

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            clearChannels(synth);

            if (loader.load(path, &sched))
                fail("an instrument naming an arg its graph does not "
                     "declare loaded anyway");
            else if (loader.errors().size() != 1)
            {
                std::ostringstream s;

                s << "one bad instrument produced "
                  << loader.errors().size() << " errors, not one";
                fail(s.str());
            }

            /* `first' came up before `bad' failed, and must not still be
               there. Drained first, because what unapplyInstrument does
               is queue a command like everything else. */
            drainSynth();

            if (synth->getChanArg(0, "fmin") != NULL)
                fail("a piece that failed to load left an instrument on a "
                     "channel");

            if (synth->getChanArg(2, "fmin") != NULL)
                fail("the instruments after the failure were loaded too");

            /* And `bad' itself, which is the one the first version of
               this check quietly skipped. Its .dsp loads fine and it is
               refused for an arg the graph does not declare -- so its
               graph is on the channel at the moment it fails, and a
               rollback that counted only successes walked straight past
               it. applyInstrument takes it back itself now, which is
               what makes the count above exact. */
            if (synth->getChanArg(1, "fmin") != NULL)
                fail("the instrument that failed was left on its channel");

            std::filesystem::remove(path);
        }
    }

    /* A channel that would not go.
     *
     * Taking an instrument off means telling the audio thread, and the
     * command ring can be full -- it is wedged, or nothing is draining
     * it, which is a harness's normal state. The graph is then still
     * loaded and still sounding while the piece that asked for it is
     * being thrown away, so the scheduler keeps the instrument and tries
     * again on its own clock. Dropping the record instead would leave a
     * graph nothing in the program could name, and headless there is no
     * window keeping a second copy.
     *
     * Driven through apply/unapply rather than a .gen, because a ring
     * full enough to block the unload blocks the *load* too and there
     * would be nothing to strand. */
    {
        thcScheduler sched(synth);
        thcInstrument inst;

        clearChannels(synth);

        inst.name = "pad";
        inst.dsp = "amb01.dsp";
        inst.channel = 0;

        sched.addInstrument(inst);

        std::string why;

        if (!sched.applyInstrument(0, why))
            fail("the stranding check could not get its instrument up: " +
                 why);
        else
        {
            drainSynth();

            /* Fill the ring, without draining. TH_COMMAND_QUEUE_SIZE
               notes would do it exactly; twice that is slack against the
               size ever changing.

               The "command queue full" flood this prints is the check
               working, and is now the only one in a passing run -- said
               out loud because it used to be one wall of it among
               several, and a reader had no way to tell the deliberate
               one from the accidents. */
            fprintf(stderr, "gencheck: filling the command ring on "
                    "purpose; the next lines are meant to be here\n");

            for (int i = 0; i < TH_COMMAND_QUEUE_SIZE * 2; i++)
                synth->addNote(0, 60, 100);

            if (sched.unapplyInstrument(0))
                fail("an unload succeeded with the command ring full");

            if (sched.strandedCount() != 1)
                fail("a channel that would not go was not remembered");

            /* Asking twice must not remember it twice. */
            sched.unapplyInstrument(0);

            if (sched.strandedCount() != 1)
                fail("the same stranded channel was recorded twice");

            /* And the retry, on the clock the harness has. */
            drainSynth();

            fprintf(stderr, "gencheck: ...and that is the end of them\n");

            sched.stepTransport(0.0);

            if (sched.strandedCount() != 0)
                fail("a stranded channel was never retried");

            drainSynth();

            if (synth->getChanArg(0, "fmin") != NULL)
                fail("the retried channel is still loaded");
        }
    }
}

/* ---- 6e. embedded nodes: dsp plugins as chain stages ------------------- */

/* DSP plugins as chain stages. Four claims:
 *
 * 1. A node's output reaches a composer param, and moves it. The whole
 *    deliverable is "an LFO breathing a chain's density", and a binding
 *    that resolved but never changed anything would look identical from
 *    outside.
 * 2. It replays. Nodes step on transport time, so the same file and the
 *    same seed deliver the same stream twice with a reset between --
 *    which is the promise every composer already keeps and the reason a
 *    node had to be as repeatable as one before it was let in.
 * 3. The refusals: families that mean nothing one sample at a time, the
 *    one plugin that cannot replay, and arrows pointing at nothing.
 * 4. Pause freezes them, because transport time is what they read.
 */
static void
checkNodes (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth, const std::string &genFile)
{
    const std::string breath =
        (std::filesystem::path(genFile).parent_path() / "breath.gen").string();

    if (!std::filesystem::exists(breath))
    {
        fail("breath.gen is not beside " + genFile + "; the node checks "
             "have nothing to run");
        return;
    }

    clearChannels(synth);

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(breath, &sched))
    {
        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

        fail("breath.gen does not load");
        return;
    }

    /* The binding is live: the param a node drives has to *move*. Read
       straight off the store, because that is where a composer reads it
       and the arithmetic in between is the piece's business. */
    thcChain *c = sched.chain(0);

    if (c == NULL || c->stages.empty() || !c->nodes)
        fail("breath.gen's first chain has no nodes in it");
    else
    {
        thcStage *src = c->stages.back().get();
        const int pi = src->plugin->paramIndex("prob");

        if (pi < 0)
            fail("the node-driven stage has no prob param");
        else
        {
            double lo = 2, hi = -1;

            sched.start();

            for (int i = 0; i < 1200; i++)      /* twenty-four seconds  */
            {
                sched.stepTransport(0.02);

                const double v = src->params.get(pi);

                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }

            sched.stop();

            /* The piece asks for 0.5 +- 0.45 over a twenty-second
               cycle, so a full swing is most of 0.05..0.95. Loose
               bounds: what is being asked is "did the LFO drive it",
               not "is osc::simple accurate", which hostcheck owns. */
            if (hi - lo < 0.5)
            {
                std::ostringstream s;

                s << "the LFO did not breathe the density: prob stayed "
                  << "between " << lo << " and " << hi;
                fail(s.str());
            }

            /* And it is the *node* doing it, not a stored value that
               happens to vary: nothing else in that chain touches
               prob. */
            if (src->params.nodeBinding(pi) == NULL)
                fail("prob is not bound to a node at all");
        }
    }

    drainSynth();

    /* Replay. Same shape as the gate airports.gen passes, pointed at
       the piece whose values come out of a second interpreter. */
    {
        thcScheduler a(synth);
        thcGenLoader la(plugins);

        clearChannels(synth);

        if (!la.load(breath, &a))
            fail("breath.gen did not load for the replay check");
        else
        {
            const std::string first = render(a, 90.0, 0.02);

            a.reset();

            const std::string second = render(a, 90.0, 0.02);

            if (first.empty())
                fail("breath.gen delivered nothing at all");
            else if (first != second)
            {
                fail("a piece with dsp nodes in it did not replay");

                size_t n = 0;

                while (n < first.size() && n < second.size() &&
                       first[n] == second[n])
                    n++;

                size_t line0 = first.rfind('\n', n);

                line0 = line0 == std::string::npos ? 0 : line0 + 1;

                fprintf(stderr, "  first : %.60s\n", first.c_str() + line0);
                fprintf(stderr, "  second: %.60s\n", second.c_str() + line0);
            }
        }
    }

    drainSynth();

    /* Pause freezes them. The nodes read transport time, and a stopped
       transport does not advance -- so the value a param sees must be
       the same before and after a stretch of wall clock spent stopped.
       Stepping a stopped scheduler is exactly what the app's timer does
       while paused. */
    {
        thcScheduler p(synth);
        thcGenLoader lp(plugins);

        clearChannels(synth);

        if (lp.load(breath, &p))
        {
            thcChain *pc = p.chain(0);
            thcStage *src = pc != NULL && !pc->stages.empty()
                ? pc->stages.back().get() : NULL;
            const int pi = src != NULL
                ? src->plugin->paramIndex("prob") : -1;

            if (src != NULL && pi >= 0)
            {
                p.start();

                for (int i = 0; i < 150; i++)
                    p.stepTransport(0.02);

                p.stop();

                const double held = src->params.get(pi);

                for (int i = 0; i < 500; i++)
                    p.stepTransport(0.02);

                if (src->params.get(pi) != held)
                    fail("a paused transport did not freeze the nodes");
            }
        }
    }

    drainSynth();

    /* A pure function of transport time, and nothing else.
     *
     * The replay above is necessary and not sufficient: the harness
     * drives both renders with the same call pattern, so a host firing
     * one window per *call* rather than per fiftieth of a second would
     * replay perfectly and still be wrong -- wrong in the way that
     * matters, since the application's dt jitters with the frame and a
     * piece would breathe at whatever rate the machine felt like. That
     * mistake was made while writing this, and the replay gate did not
     * notice.
     *
     * So it is asked directly, which is the only place the question is
     * separable from what the composers around it are doing: one graph,
     * one span of transport, reached in a hundred and fifty steps and in
     * a single jump. Same answer, or the clock is not the clock. Three
     * seconds because that is inside the catch-up guard -- a longer jump
     * is deliberately allowed to skip, the way a pause is.
     */
    {
        /* The host's own control-rate synth, made here the way the
           scheduler makes one: a rate and a plugin manager. */
        thSynth control(synth->getPluginManager()->pluginPath(), 1, 50);
        thcNodeHost a(&control, 50);
        thcNodeHost b(&control, 50);
        std::string why;
        bool built = true;

        for (int which = 0; which < 2; which++)
        {
            thcNodeHost &h = which == 0 ? a : b;

            built = built && h.addNode("lfo", "osc/simple", why);
            h.setValue("lfo", "freq", 0.37, why);
            h.setValue("lfo", "waveform", 0, why);
            h.setValue("lfo", "amp", 1, why);
            built = built && h.build(why);
        }

        if (!built)
            fail("the clock check could not build its graph: " + why);
        else
        {
            thArg *ao = a.output("lfo", "out", why);
            thArg *bo = b.output("lfo", "out", why);

            for (int i = 0; i < 150; i++)
                a.stepTo((i + 1) * 0.02);

            b.stepTo(3.0);

            if (ao == NULL || bo == NULL)
                fail("the clock check lost its outputs: " + why);
            else if ((*ao)[0] != (*bo)[0])
            {
                std::ostringstream s;

                s << "a node's value depends on how the transport was "
                  << "reached rather than on where it got to: stepped "
                  << (*ao)[0] << ", jumped " << (*bo)[0];
                fail(s.str());
            }
            else if ((*ao)[0] == 0)
                fail("the clock check watched a signal that never moved");
        }
    }

    /* A literal typed over a node-driven param actually takes.
     *
     * The panel's undo path: somebody selects a stage whose `prob' reads
     * an LFO, types 0.9, and the file, the canvas and the panel all say
     * 0.9 from that moment. Releasing only the knob left the node still
     * shadowing the stored value, so the piece went on breathing while
     * every surface that could show a number showed the new one -- and
     * saving and reopening sounded different from what had just been
     * heard, which is the part that makes it worth a gate rather than a
     * bug report. Driven through the scheduler primitive the panel
     * calls, since gencheck links no widgets.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(breath, &sched))
            fail("breath.gen did not load for the unbind check");
        else
        {
            thcChain *c = sched.chain(0);
            thcStage *src = c != NULL && !c->stages.empty()
                ? c->stages.back().get() : NULL;
            const int idx = src != NULL
                ? src->plugin->paramIndex("prob") : -1;

            if (src == NULL || idx < 0)
                fail("breath.gen's first chain no longer ends in a stage "
                     "with a 'prob' param");
            else if (src->params.nodeBinding(idx) == NULL)
                fail("breath.gen's 'prob' is not node-driven any more, so "
                     "the unbind check is testing nothing");
            else
            {
                /* Somewhere the LFO is not, so "it took" cannot be read
                   off a value the node might have produced anyway. */
                sched.start();

                for (int i = 0; i < 40; i++)
                    sched.stepTransport(0.02);

                sched.unbindParam(src, idx);
                src->params.set(idx, 0.9);

                if (src->params.nodeBinding(idx) != NULL)
                    fail("unbindParam left the node binding in place");

                bool moved = false;

                for (int i = 0; i < 40; i++)
                {
                    sched.stepTransport(0.02);

                    if (fabs(src->params.get(idx) - 0.9) > 1e-6)
                        moved = true;
                }

                if (moved)
                    fail("a value written over a node-driven param was "
                         "still shadowed by the node");

                sched.stop();
            }
        }

        clearChannels(synth);
    }

    /* ---- the refusals ---- */

    expectReject(plugins, synth, "node-bad-family",
        "chain c { stage d dist::clip { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "not a family that means anything at control rate");

    expectReject(plugins, synth, "node-samples-family",
        "chain c { stage e delay::echo { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "counts in samples");

    expectReject(plugins, synth, "node-not-replayable",
        "chain c { stage n osc::static { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "would not replay");

    expectReject(plugins, synth, "node-no-such-module",
        "chain c { stage n osc::nosuchosc { };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "nosuchosc");

    /* An arrow to a node nobody declared, and to an arg it does not
       have. Both are the composer-side spelling, which is where a
       reader is most likely to get it wrong. */
    expectReject(plugins, synth, "arrow-no-node",
        "chain c { stage s gen::eno_line { prob = ghost->out; };"
        " sink { channel = 1; }; };",
        "no dsp stages");

    expectReject(plugins, synth, "arrow-no-arg",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { prob = lfo->nosucharg; };"
        " sink { channel = 1; }; };",
        "nosucharg");

    /* An arg the module does not declare. thNode::setArg invents one,
       which is right for a .dsp and silent here: `frq' for `freq' gave
       an oscillator running at zero and a piece that simply did not
       breathe, with no error anywhere. */
    expectReject(plugins, synth, "node-bad-arg",
        "chain c { stage lfo osc::simple { frq = 0.05; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "no arg called 'frq'");

    /* Both ends of a wire, not just the near one. setPointers would
       otherwise create the missing far arg as a permanent zero. */
    expectReject(plugins, synth, "wire-bad-far-arg",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage m math::mul { in0 = lfo->nosuch; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "no arg called 'nosuch'");

    /* A module's own scratch is not a port. */
    expectReject(plugins, synth, "node-state-arg",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { prob = lfo->last; };"
        " sink { channel = 1; }; };",
        "own scratch");

    /* Nor is an input something to read out of. */
    expectReject(plugins, synth, "arrow-at-an-input",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { prob = lfo->freq; };"
        " sink { channel = 1; }; };",
        "is an input, not an output");

    /* And an output is not something to write into -- the other end of
       the same arrow, in its three spellings.
     *
       The third one is why these are worth having. A node pointing its
       own output at itself built a thArg whose pointer resolves to
       itself, and thSynthTree::getArg follows a pointer in a loop with
       no exit: the file loaded without a word of complaint and the
       first window hung the GUI thread. The other two were quiet
       instead of fatal -- a value the plugin overwrote every window,
       and a module handed somebody else's buffer to write into -- which
       is the same silence a mistyped arg name used to produce, and the
       reason checkArg exists at all. */
    expectReject(plugins, synth, "value-at-an-output",
        "chain c { stage lfo osc::simple { freq = 1; out = 0.5; };"
        " stage s gen::eno_line { prob = lfo->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    expectReject(plugins, synth, "wire-into-an-output",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage m math::mul { in0 = lfo->out; in1 = 2; out = lfo->out; };"
        " stage s gen::eno_line { prob = m->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    expectReject(plugins, synth, "node-wired-to-itself",
        "chain c { stage m math::mul { in0 = 1; in1 = 2; out = m->out; };"
        " stage s gen::eno_line { prob = m->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    /* A knob cannot write one either -- the same end of the arrow,
       reached from the shared binding namespace. */
    expectReject(plugins, synth, "knob-at-an-output",
        "@depth = 0.5;\n@depth.min = 0;\n@depth.max = 1;\n"
        "chain c { stage lfo osc::simple { freq = 1; out = @depth; };"
        " stage s gen::eno_line { prob = lfo->out; };"
        " sink { channel = 1; }; };",
        "cannot write it");

    /* In an allowed family and refused by name: filt:: is admitted
       because a filter is a shape over time, and comb is a delay line
       whose `size' is a raw sample count. Same criterion delay:: and
       fft:: are refused by, arriving one category later. */
    expectReject(plugins, synth, "node-comb-counts-samples",
        "chain c { stage k filt::comb { in = 0.5; size = 4410; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "delay line");

    /* The name this host gives the node it invents. */
    expectReject(plugins, synth, "node-called-ionode",
        "chain c { stage ionode osc::simple { freq = 1; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "call it something else");

    /* And a wire between nodes pointing at nothing, which is the same
       mistake one level down. */
    expectReject(plugins, synth, "wire-no-node",
        "chain c { stage m math::mul { in0 = ghost->out; };"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "there is no node called 'ghost'");

    /* A node cannot drive something that is not a number, for the same
       reason a knob cannot. */
    expectReject(plugins, synth, "arrow-on-noteset",
        "chain c { stage lfo osc::simple { freq = 1; };"
        " stage s gen::eno_line { notes = lfo->out; };"
        " sink { channel = 1; }; };",
        "a node cannot drive it");

    clearChannels(synth);
}

/* ---- 6f. structure edits: composers reshaping instruments -------------- */

/* Composers reshaping instruments. What has to be true:
 *
 * 1. A swap actually swaps -- the channel is playing a different graph
 *    afterwards, not merely told to.
 * 2. A node-arg edit reaches a constant the patch never declared, which
 *    is the whole of what makes it a different mechanism from a chanarg
 *    rather than a wider one.
 * 3. Both replay. They are events, so they should; a piece full of them
 *    rendered twice with a reset between must be byte-identical.
 * 4. A rewind puts the instruments back as the file declared them,
 *    because after a swap the channels no longer say what the file says.
 * 5. The refusals -- an instrument nobody declared, an arg no node has,
 *    a node arg that is wired rather than constant.
 */
static void
checkStructureEdits (const std::map<std::string, thcPlugin *> &plugins,
                     thSynth *synth, const std::string &genFile)
{
    const std::string piece =
        (std::filesystem::path(genFile).parent_path() / "reshape.gen").string();

    if (!std::filesystem::exists(piece))
    {
        fail("reshape.gen is not beside " + genFile);
        return;
    }

    clearChannels(synth);

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail("reshape.gen does not load");
            return;
        }

        /* voice is amb01 and glass is ts1, and only one of them declares
           `cutoff'. So which graph is on the channel is a question with
           an answer, rather than a thing to take on trust. */
        const thcInstrument *voice = sched.instrument("voice");
        const thcInstrument *glass = sched.instrument("glass");

        if (voice == NULL || glass == NULL)
            fail("reshape.gen no longer declares voice and glass");
        else
        {
            const int ch = voice->channel;

            drainSynth();

            if (synth->getChanArg(ch, "fmin") == NULL)
                fail("the piece did not open on the instrument it declares");

            std::string why;

            if (!sched.swapInstrument(ch, "glass", why))
                fail("a swap to glass was refused: " + why);

            drainSynth();

            /* ts1 has a cutoff and amb01 does not: the graph changed. */
            if (synth->getChanArg(ch, "cutoff") == NULL)
                fail("after the swap the channel is not playing glass");

            if (synth->getChanArg(ch, "fmin") != NULL)
                fail("after the swap the old graph is still there");

            /* And the values came with it, not just the graph. */
            thArg *res = synth->getChanArg(ch, "res");

            if (res == NULL || fabs((*res)[0] - 0.5) > 1e-4)
                fail("the swapped-in instrument did not bring its values");

            /* Back, and then the fine edit -- which needs amb01, since
               `fmap' is one of its nodes. */
            if (!sched.swapInstrument(ch, "voice", why))
                fail("a swap back to voice was refused: " + why);

            drainSynth();

            if (!sched.setNodeArg(ch, "fmap", "inmax", 0.25f, why))
                fail("a node arg the patch never declared was refused: " +
                     why);
            else
            {
                /* Read back off the prototype tree, which is where a
                   new voice would read it. */
                thMidiChan *c = synth->getChannel(ch);
                thSynthTree *tree = c != NULL ? c->modnode() : NULL;
                thNode *n = tree != NULL ? tree->findNode("fmap") : NULL;
                thArg *a = n != NULL ? n->getArg("inmax") : NULL;

                if (a == NULL || fabs((*a)[0] - 0.25) > 1e-5)
                    fail("the node arg did not take");

                /* And it really is not a chanarg -- if it were, this
                   whole mechanism would be a chanarg with extra steps. */
                if (synth->getChanArg(ch, "inmax") != NULL)
                    fail("'inmax' is a chanarg after all, which would make "
                         "this the wrong test entirely");
            }

            /* A rewind puts the file back. */
            sched.reset();
            drainSynth();

            thNode *n2 = NULL;
            thMidiChan *c2 = synth->getChannel(ch);
            thSynthTree *t2 = c2 != NULL ? c2->modnode() : NULL;

            if (t2 != NULL)
                n2 = t2->findNode("fmap");

            thArg *a2 = n2 != NULL ? n2->getArg("inmax") : NULL;

            if (a2 != NULL && fabs((*a2)[0] - 0.25) < 1e-5)
                fail("a rewind left a structure edit in place");
        }
    }

    drainSynth();

    /* Replay, the gate every piece passes and the one a structure edit
       had to earn before it was allowed to exist. */
    {
        clearChannels(synth);

        thcScheduler a(synth);
        thcGenLoader la(plugins);

        if (!la.load(piece, &a))
            fail("reshape.gen did not load for the replay check");
        else
        {
            const std::string first = render(a, 130.0, 0.02);

            a.reset();

            const std::string second = render(a, 130.0, 0.02);

            if (first.empty())
                fail("reshape.gen delivered nothing at all");
            else if (first.find("P ") == std::string::npos)
                fail("no structure edits in reshape.gen's stream");
            else if (first != second)
                fail("a piece with structure edits in it did not replay");

            /* And the sweep moves on every step it takes.
             *
               A ping-pong that reflects *at* its walls rather than one
               short of them emits its top value twice running, and its
               bottom twice running -- a cycle two ticks long than the
               file asks for that stalls at each extreme. Nothing about
               the replay gate can see that: both renders stall
               identically. Consecutive equal values on one arg is the
               shape of the bug, said directly. */
            std::map<std::string, std::string> last;
            std::istringstream lines(first);
            std::string line;

            while (std::getline(lines, line))
            {
                if (line.compare(0, 2, "E ") != 0)
                    continue;

                /* "E <at> <ch> <node> <arg> <value>": node and arg
                   together are which constant this is, and the value is
                   what has to have moved since last time. */
                std::istringstream f(line);
                std::string tag, at, ch, node, arg, val;

                if (!(f >> tag >> at >> ch >> node >> arg >> val))
                    continue;

                const std::string key = ch + " " + node + "." + arg;

                if (last.count(key) && last[key] == val)
                {
                    fail("a node-arg sweep emitted '" + val +
                         "' twice running for " + key);
                    break;
                }

                last[key] = val;
            }

            if (last.empty())
                fail("no node-arg edits in reshape.gen's stream");
        }
    }

    drainSynth();
    clearChannels(synth);

    /* ---- what `pass = 0' is allowed to eat ---- */

    /* A transformer told not to pass notes eats the notes, and nothing
     * else.
     *
     * Two halves of one rule. A stage consuming what it is *for* is a
     * design; a stage consuming everything that happens to flow through
     * it is a hole in a pipeline -- a gen::reshape in front of a
     * `pass = 0' arp delivered nothing at all, silently, and the piece
     * simply never changed. And the off goes with its on: forwarding a
     * release whose press this stage ate is an off for a note it did not
     * play, which the scheduler hands to delNote and which then silences
     * whatever else is sounding at that pitch.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);
        std::string tmp = thUtil::tempFile("gencheck-passrule-");

        if (tmp.empty())
            fail("the pass-rule check could not make a scratch file");
        else
        {
            {
                std::ofstream out(tmp.c_str(), std::ios::trunc);

                out << "seed 7;\n"
                    << "instrument pad { dsp \"amb01.dsp\"; };\n"
                    << "chain edits {\n"
                    << "  stage r gen::reshape { node = \"fmap\";"
                    << " arg = \"inmax\"; from = 1; to = 0.25;"
                    << " every = 1 s; steps = 4; };\n"
                    << "  stage a xform::arp { pass = 0; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n"
                    << "chain keys {\n"
                    << "  input midi;\n"
                    << "  stage m xform::markov { pass = 0; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n"
                    /* All three transformers that carry the rule, since
                       the fix landed on one of them first and the other
                       two kept the bug for a release. */
                    << "chain keys2 {\n"
                    << "  input midi;\n"
                    << "  stage l xform::life { pass = 0; listen = 1; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n"
                    << "chain keys3 {\n"
                    << "  input midi;\n"
                    << "  stage p xform::arp { pass = 0; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n";

                if (!out.good())
                    fail("the pass-rule check could not write " + tmp);
            }

            if (!loader.load(tmp, &sched))
            {
                for (size_t i = 0; i < loader.errors().size(); i++)
                    fprintf(stderr, "gencheck: %s\n",
                            loader.errors()[i].c_str());

                fail("the pass-rule piece did not load");
            }
            else
            {
                int edits = 0, notes = 0;

                sigc::connection conn = sched.sigDelivered.connect(
                    [&edits, &notes](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NODEARG)
                            edits++;
                        else if (ev.type == THC_EV_NOTE ||
                                 ev.type == THC_EV_NOTEOFF)
                            notes++;
                    });

                sched.start();

                /* The press and its release, both of which the markov
                   stage is told to eat. */
                thcEvent key = {};

                key.type = THC_EV_NOTE;
                key.u.note.level = 1;
                key.channel = 0;
                key.u.note.note = 60;
                key.u.note.velocity = 90;
                key.u.note.duration = 0;
                key.at = sched.now();
                sched.injectMidiEvent(key);

                sched.stepTransport(0.1);

                thcEvent up = {};

                up.type = THC_EV_NOTEOFF;
                up.channel = 0;
                up.u.note.note = 60;
                up.at = sched.now();
                sched.injectMidiEvent(up);

                for (int i = 0; i < 200; i++)
                    sched.stepTransport(0.02);

                sched.stop();
                conn.disconnect();
                drainSynth();

                if (edits == 0)
                    fail("a 'pass = 0' transformer swallowed the structure "
                         "edits of the stage in front of it");

                if (notes != 0)
                    fail("a 'pass = 0' transformer ate a press and "
                         "forwarded its release anyway");
            }

            std::filesystem::remove(tmp);
        }

        clearChannels(synth);
    }

    /* ---- two events at one instant come out the way they went in ---- */

    /* A heap does not order equal keys, and a re-pressed held root makes
     * a pile of exactly equal keys: xform::harmonize releases the old
     * chord at `at + spread*v' and presses the new one at `at + spread*v'
     * -- the same instant, deliberately, because that is what replacing a
     * chord means. Popped in heap order an on could be delivered ahead of
     * the off emitted before it, and deliver() then ran addNote followed
     * by delNote on the same pitch: the voice was created and immediately
     * killed, and held_ kept an entry for a note nothing was playing.
     *
     * Asserted as the harm rather than as the ordering, because the
     * ordering is only interesting for what it does: walk the delivered
     * stream in order, and every pitch of a chord nobody released has to
     * end up sounding.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);
        std::string tmp = thUtil::tempFile("gencheck-tieorder-");

        if (tmp.empty())
            fail("the tie-order check could not make a scratch file");
        else
        {
            {
                std::ofstream out(tmp.c_str(), std::ios::trunc);

                out << "seed 3;\n"
                    << "instrument pad { dsp \"amb01.dsp\"; };\n"
                    << "chain keys {\n"
                    << "  input midi;\n"
                    /* spread 0 so every voice collides, which is the
                       case the heap was free to reorder. */
                    << "  stage h xform::harmonize { voices = 3;"
                    << " spread = 0 s; root = 1; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n";

                if (!out.good())
                    fail("the tie-order check could not write " + tmp);
            }

            if (!loader.load(tmp, &sched))
                fail("the tie-order piece did not load");
            else
            {
                std::map<int, bool> sounding;
                int offs = 0;

                sigc::connection conn = sched.sigDelivered.connect(
                    [&sounding, &offs](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NOTE)
                            sounding[ev.u.note.note] = true;
                        else if (ev.type == THC_EV_NOTEOFF)
                        {
                            sounding[ev.u.note.note] = false;
                            offs++;
                        }
                    });

                sched.start();

                thcEvent key = {};

                key.type = THC_EV_NOTE;
                key.u.note.level = 1;
                key.channel = 0;
                key.u.note.note = 60;
                key.u.note.velocity = 90;
                key.u.note.duration = 0;    /* held */
                key.at = sched.now();
                sched.injectMidiEvent(key);

                for (int i = 0; i < 10; i++)
                    sched.stepTransport(0.02);

                /* The re-press: one key, one entry, so the old chord is
                   released and the new one pressed at the same instant. */
                key.at = sched.now();
                sched.injectMidiEvent(key);

                for (int i = 0; i < 20; i++)
                    sched.stepTransport(0.02);

                /* Listening stops before the transport does. A stop
                   flushes every held note and says so -- an off per key
                   on sigDelivered, which is how the roll learns a bar
                   it is drawing has ended (thcScheduler::flushHeld).
                   Those are real releases and nothing to do with the
                   re-press this is about, and counting them would turn
                   "nobody released the chord" into a statement about
                   teardown. */
                conn.disconnect();
                sched.stop();
                drainSynth();

                if (offs == 0)
                    fail("the tie-order check saw no releases, so the "
                         "re-press it is about did not happen");

                if (sounding.empty())
                    fail("the tie-order check heard nothing at all");

                for (std::map<int, bool>::const_iterator i = sounding.begin();
                     i != sounding.end(); ++i)
                    if (!i->second)
                    {
                        std::ostringstream s;

                        s << "a re-pressed chord ended with pitch "
                          << i->first << " released: an on was delivered "
                          << "before the off emitted ahead of it";
                        fail(s.str());
                        break;
                    }
            }

            std::filesystem::remove(tmp);
        }

        clearChannels(synth);
    }

    /* ---- what a rewind reaches, and what it leaves alone ---- */

    /* A rewind restores the channels a structure edit touched and no
     * others.
     *
     * applyInstrument goes through the host's patch loader, which drops
     * the channel and re-parses the .dsp -- so re-applying every
     * declaration because *one* of them moved threw away hand-tuned
     * values and disarmed probes on channels nothing had been near, and
     * did it only once a swap had happened to fire. Rewind behaving
     * differently depending on how far the piece had got is the part
     * worth a gate: the cheap way to see it is that an untouched
     * channel's graph is the same object afterwards, where a restored
     * one is not.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
            fail("reshape.gen did not load for the rewind-scope check");
        else
        {
            const thcInstrument *voice = sched.instrument("voice");
            const thcInstrument *other = sched.instrument("bell");

            if (voice == NULL || other == NULL ||
                voice->channel == other->channel)
                fail("reshape.gen no longer declares voice and bell on "
                     "channels of their own");
            else
            {
                drainSynth();

                thMidiChan *before = synth->getChannel(other->channel);
                std::string why;

                /* One edit, on voice's channel only. */
                if (!sched.setNodeArg(voice->channel, "fmap", "inmax",
                                      0.25f, why))
                    fail("the rewind-scope check could not make its edit: " +
                         why);

                sched.reset();
                drainSynth();

                if (synth->getChannel(other->channel) != before)
                    fail("a rewind rebuilt a channel no structure edit had "
                         "touched");

                thMidiChan *c = synth->getChannel(voice->channel);
                thSynthTree *t = c != NULL ? c->modnode() : NULL;
                thNode *n = t != NULL ? t->findNode("fmap") : NULL;
                thArg *a = n != NULL ? n->getArg("inmax") : NULL;

                if (n == NULL || a == NULL)
                    fail("a rewind left the edited channel without the "
                         "graph its declaration names");
                else if (fabs((*a)[0] - 0.25) < 1e-5)
                    fail("a rewind left a structure edit in place");
            }
        }

        clearChannels(synth);
    }

    /* ---- a list the panel wrote rather than the loader ---- */

    /* The loader normalises an instrument list to "voice,bell,glass" and
     * checks every name in it, so a piece read off disk never exercises
     * what a composer does with the separators. The param panel is the
     * other writer, and it stores what was typed: a list edited in the
     * window to "voice, bell, glass" reaches the plugin with the spaces
     * still in it. Splitting on commas alone made that a name with a
     * space welded to the front, and every swap to it was refused by a
     * service that had never heard of " bell" -- a piece that quietly
     * stopped swapping, with nothing in the log tying it to the edit
     * that did it.
     *
     * Asserted on the event rather than on the tape, because the tape is
     * whitespace-separated and reading a name back out of it would eat
     * the very space this is about.
     */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
            fail("reshape.gen did not load for the spaced-list check");
        else
        {
            thcStage *swap = NULL;

            for (size_t i = 0; i < sched.chainCount() && swap == NULL; i++)
            {
                thcChain *c = sched.chain(i);

                for (size_t j = 0; c != NULL && j < c->stages.size(); j++)
                    if (c->stages[j]->plugin->name() == "swap")
                    {
                        swap = c->stages[j].get();
                        break;
                    }
            }

            if (swap == NULL)
                fail("reshape.gen no longer has a gen::swap stage");
            else
            {
                const int idx = swap->plugin->paramIndex("instruments");

                /* Exactly what ComposerWindow::applyParam does to a live
                   stage: the typed text, stored as typed, then the
                   changed notification the panel sends after it. */
                if (idx < 0 ||
                    !swap->params.setString("instruments",
                                            "voice, bell, glass"))
                    fail("gen::swap has no 'instruments' param");
                else
                {
                    swap->params.notifyChanged(idx);

                    std::vector<std::string> swapped;
                    sigc::connection conn = sched.sigDelivered.connect(
                        [&swapped](const thcEvent &ev)
                        {
                            if (ev.type == THC_EV_PATCH)
                                swapped.push_back(ev.u.patch.name
                                                  ? ev.u.patch.name : "");
                        });

                    sched.start();

                    while (sched.now() < 130.0)
                        sched.stepTransport(0.02);

                    sched.stop();
                    conn.disconnect();
                    drainSynth();

                    for (size_t i = 0; i < swapped.size(); i++)
                        if (sched.instrument(swapped[i]) == NULL)
                            fail("a spaced instrument list swapped to '" +
                                 swapped[i] + "', which the piece does "
                                 "not declare");

                    if (swapped.empty())
                        fail("a spaced instrument list produced no swaps");
                }
            }
        }
    }

    drainSynth();
    clearChannels(synth);

    /* ---- the refusals ---- */

    expectReject(plugins, synth, "swap-unknown-instrument",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage m gen::swap { instruments = ghost; };"
        " sink { instrument = pad; }; };",
        "no instrument called 'ghost'");

    expectReject(plugins, synth, "swap-unknown-in-list",
        "instrument pad { dsp \"amb01.dsp\"; };\n"
        "chain c { stage m gen::swap { instruments = \"pad,ghost\"; };"
        " sink { instrument = pad; }; };",
        "no instrument called 'ghost'");

    /* The services say no by name, which is what a piece hitting one
       mid-play has to read in the log. */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        /* Said rather than skipped. A block of refusals inside an
           `if (loaded)' with no else is a block that reports success the
           day the piece stops loading, which is the day you want to hear
           about it most. */
        if (!loader.load(piece, &sched))
            fail("the structure-edit piece did not load");
        else
        {
            const thcInstrument *voice = sched.instrument("voice");
            const int ch = voice != NULL ? voice->channel : 0;
            std::string why;

            drainSynth();

            if (sched.swapInstrument(ch, "nosuchinstrument", why))
                fail("a swap to an undeclared instrument succeeded");

            /* A channel the piece declares no instrument for. A swap
               rebuilds a whole graph, so reaching one is reaching into
               somebody's loaded patch and throwing it away -- and a
               rewind would not put it back, because it is in no
               declaration to be restored from. */
            if (sched.swapInstrument(15, "bell", why))
                fail("a swap onto a channel the piece does not own "
                     "succeeded");

            /* And the fine edit onto the same channel, which had no such
               refusal and needed it more. A swap into somebody's
               hand-loaded patch is loud; rewriting one constant inside
               their graph is silent, and reset() restores by re-applying
               *declarations*, so there is nothing that would ever put it
               back.
             *
               With a graph actually on that channel, which is the whole
               point: written against an empty one this passed on
               "nothing is loaded there" and would have gone on passing
               with the refusal deleted. So put the patch a person would
               have had loaded onto it first, and then ask -- and read
               the message, because there are two ways to say no here and
               only one of them is the one under test. */
            const std::string handLoaded = thUtil::findDataFile(
                "amb01.dsp", "dsp", "THINK_DSP_PATH", DSP_PATH);

            if (handLoaded.empty() ||
                synth->loadTree(handLoaded.c_str(), 15,
                                TH_DEFAULT_CHAN_AMP) == NULL)
                fail("could not put a patch on channel 16 by hand");
            else
            {
                drainSynth();

                why.clear();

                if (sched.setNodeArg(15, "fmap", "inmax", 0.25f, why))
                    fail("a node arg on a channel the piece does not own "
                         "succeeded");
                else if (why.find("does not") == std::string::npos &&
                         why.find("not one this piece declares") ==
                             std::string::npos)
                    fail("a node arg on an undeclared channel was refused, "
                         "but for the wrong reason: " + why);
            }

            if (sched.setNodeArg(ch, "nosuchnode", "x", 1, why))
                fail("a node arg on a node that is not there succeeded");

            if (sched.setNodeArg(ch, "fmap", "nosucharg", 1, why))
                fail("a node arg the module never declared succeeded");

            /* `fmap.in' is wired to ionode->velocity. Writing a number
               over it would silently unwire the graph, which is an
               add/remove/rewire edit wearing a value edit's clothes. */
            if (sched.setNodeArg(ch, "fmap", "in", 1, why))
                fail("a wired node arg was overwritten with a constant");

            /* And the other kind of wire, which is the one the first
               draft let through: `fmap.outmin' is `@fmin'. thNode::setArg
               retypes an arg to ARG_VALUE whatever it was, and
               assignChanArgPointers only re-points args still typed
               ARG_CHANNEL -- so a number written here kills that
               channel's fmin for the rest of the session, silently. */
            if (sched.setNodeArg(ch, "fmap", "outmin", 0.5, why))
                fail("a node arg wired to a chanarg was overwritten with "
                     "a constant");

            /* And a swap to what is already there does nothing rather
               than rebuilding the graph into a copy of itself, because a
               gen::swap cannot see what its sink's channel holds: point
               one at a list whose first name is what the sink already
               plays and the opening tick would cut every sounding voice
               for no change. "Did nothing" is the prototype tree still
               being the same object -- a rebuild goes through loadTree,
               which makes a new one. */
            thMidiChan *mc = synth->getChannel(ch);
            const void *before = mc != NULL ? (void *)mc->modnode() : NULL;

            if (!sched.swapInstrument(ch, sched.holding(ch), why))
                fail("a swap to the instrument already there was refused");

            mc = synth->getChannel(ch);

            if (before == NULL ||
                before != (const void *)(mc != NULL ? mc->modnode() : NULL))
                fail("a swap to the instrument already there rebuilt the "
                     "graph anyway");

            /* A swapped-away instrument stops driving the channel it
               was on.
             *
               A knob bound into an instrument's chanarg is a *push*: the
               knob moves, the chanarg is written. The connections used
               to be appended and never removed, so swapping `quiet' onto
               `loud's channel left loud's binding pushing into it
               alongside quiet's own values -- one knob nudge and the
               instrument the file says is playing is not the one you
               hear. Both instruments are the same .dsp on purpose, so
               the stale binding finds a real arg to write through
               instead of failing to find one and looking fixed. */
            {
                const std::string body =
                    "@k = 0.4;\n@k.min = 0;\n@k.max = 1;\n"
                    "instrument loud {\n"
                    "    dsp \"amb01.dsp\";\n"
                    "    fmin = @k;\n"
                    "};\n"
                    "instrument quiet {\n"
                    "    dsp \"amb01.dsp\";\n"
                    "    fmin = 0.2;\n"
                    "};\n"
                    "chain c { stage s gen::eno_line { };"
                    " sink { instrument = loud; }; };\n";

                const std::string path = thUtil::tempFile("gencheck-swapkn-");

                if (path.empty())
                    fail("could not write the swapped-knob piece");
                else
                {
                    FILE *f = fopen(path.c_str(), "w");

                    if (f == NULL)
                        fail("could not write the swapped-knob piece");
                    else
                    {
                        fputs(body.c_str(), f);
                        fclose(f);
                    }

                    clearChannels(synth);

                    thcScheduler s2(synth);
                    thcGenLoader l2(plugins);

                    if (!l2.load(path, &s2))
                        fail("the swapped-knob piece did not load");
                    else
                    {
                        const thcInstrument *loud = s2.instrument("loud");
                        const int lch = loud != NULL ? loud->channel : 0;
                        std::string w2;

                        drainSynth();

                        if (!s2.swapInstrument(lch, "quiet", w2))
                            fail("swapping quiet in failed: " + w2);
                        else
                        {
                            drainSynth();

                            thArg *k = s2.knob("k");
                            thArg *fmin = synth->getChanArg(lch, "fmin");

                            if (k == NULL || fmin == NULL)
                                fail("the swapped-in instrument did not "
                                     "arrive whole");
                            else
                            {
                                k->setValue(0.9f);

                                if (fabs((*fmin)[0] - 0.2) > 1e-5)
                                    fail("a swapped-away instrument's knob "
                                         "binding still drives the channel");
                            }
                        }
                    }

                    remove(path.c_str());
                }

                clearChannels(synth);
            }

        }
    }

    clearChannels(synth);
}

/* How many notes a scratch piece delivers in `seconds'. The two things
 * a board fed notes can be asked -- "did it draw" and "did it refuse to
 * draw" -- are both this number against zero. */
static int
notesFrom (const std::map<std::string, thcPlugin *> &plugins,
           thSynth *synth, const std::string &what,
           const std::string &body, double seconds)
{
    const std::string path = thUtil::tempFile("gencheck-plays-");

    if (path.empty())
    {
        fail("could not write the piece for " + what);
        return -1;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    clearChannels(synth);
    drainSynth();

    int n = -1;

    {
        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(path, &sched))
        {
            for (size_t i = 0; i < loader.errors().size(); i++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[i].c_str());

            fail(what + " did not load");
        }
        else
        {
            std::istringstream lines(render(sched, seconds, 0.02));
            std::string line;

            n = 0;

            while (std::getline(lines, line))
                if (line.compare(0, 2, "N ") == 0)
                    n++;
        }
    }

    remove(path.c_str());
    clearChannels(synth);

    return n;
}

/* ---- 7. a chain that is a pipeline ------------------------------------- */

/* colony.gen, and the two things it needed that did not exist.
 *
 * gen::life gained a receive, so an upstream stage can draw on the board
 * the way a mouse already could; xform::harmonize turns a note into a
 * chord counted in scale degrees. Both are easy to write in a way that
 * looks right and does nothing, so both are asked directly.
 *
 * 1. Feeding the board CHANGES WHAT IT PLAYS. This is the claim the
 *    piece's header makes and the one worth defending: the same board,
 *    rendered with the line and without it, must not produce the same
 *    stream -- and with it must reach rows the blinker alone never
 *    touches. A receive that quietly dropped every note would pass a
 *    "does it still load" gate forever.
 * 2. A pitch the ladder cannot spell draws nothing, rather than landing
 *    on the nearest row it can find.
 * 3. `listen = 0' is the pure generator glider.gen still wants.
 * 4. The chord is DIATONIC. Two degrees above the first pentatonic
 *    degree and two above the second are different numbers of
 *    semitones; a harmonizer that added a fixed interval would give the
 *    same gap everywhere and is the thing this must not be.
 * 5. `voices = 1' passes the note through untouched, `root = 0' drops
 *    it, and `below = 1' puts the harmony underneath.
 * 6. The whole piece replays.
 */
static void
checkColony (const std::map<std::string, thcPlugin *> &plugins,
             thSynth *synth, const std::string &genFile)
{
    const std::string piece =
        (std::filesystem::path(genFile).parent_path() / "colony.gen").string();

    if (!std::filesystem::exists(piece))
    {
        fail("colony.gen is not beside " + genFile);
        return;
    }

    clearChannels(synth);

    /* ---- 1. the line makes a difference ---- */

    /* Two renders of the same file, one with the line's onsets turned
       off. Editing the piece rather than writing a fresh one on purpose:
       what is being gated is the shipped configuration, and a scratch
       file tuned until it passed would gate nothing about it. */
    {
        const std::string text = slurp(piece);

        if (text.find("fills = 7;") == std::string::npos)
            fail("colony.gen no longer spells its euclid's fills the way "
                 "the gate looks for");
        else
        {
            std::string silent = text;
            const size_t at = silent.find("fills = 7;");

            silent.replace(at, strlen("fills = 7;"), "fills = 0;");

            const std::string sp = thUtil::tempFile("gencheck-colony-");

            if (sp.empty())
                fail("could not write the line-off variant");
            else
            {
                {
                    std::ofstream out(sp.c_str(), std::ios::trunc);

                    out << silent;
                }

                std::string withLine, without;
                std::set<int> pitchesWith, pitchesWithout;

                for (int pass = 0; pass < 2; pass++)
                {
                    clearChannels(synth);
                    drainSynth();

                    thcScheduler sched(synth);
                    thcGenLoader loader(plugins);

                    if (!loader.load(pass ? sp : piece, &sched))
                    {
                        fail(pass ? "the line-off variant did not load"
                                  : "colony.gen did not load");
                        break;
                    }

                    const std::string tape = render(sched, 90.0, 0.04);

                    (pass ? without : withLine) = tape;

                    /* Which rows of the board were reached. Only the
                       colony's channel counts: the floor chain plays its
                       own pitches on its own instrument and would mask
                       a board that never left its blinker. */
                    std::istringstream lines(tape);
                    std::string line;

                    while (std::getline(lines, line))
                    {
                        std::istringstream f(line);
                        std::string tag, at2, ch, note;

                        if (!(f >> tag >> at2 >> ch >> note) || tag != "N")
                            continue;

                        if (atoi(ch.c_str()) != 0)
                            continue;

                        (pass ? pitchesWithout : pitchesWith)
                            .insert(atoi(note.c_str()));
                    }
                }

                if (withLine.empty())
                    fail("colony.gen delivered nothing at all");
                else if (withLine == without)
                    fail("feeding gen::life a line changed nothing about "
                         "what it played");
                else if (pitchesWith.size() <= pitchesWithout.size())
                    fail("the line did not spread the colony past the "
                         "pitches the blinker reaches alone");

                remove(sp.c_str());
            }
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 2, 3. what the board does and does not accept ---- */

    /* One generation of a board fed four notes: two the ladder can spell
       and two it cannot. Only the spellable ones may draw. */
    /* A 4x2 board, empty, and a rhythm on the two pitches its ladder
       spells. `trigger = 1' so a drawn cell is heard the generation it
       is drawn rather than having to survive into a birth -- what is
       being asked here is whether the cell arrived at all. */
    {
        const char *shape =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; notes = \"%s\";"
            " period = 0.25 s; hold = 0.2 s; };\n"
            "  stage b gen::life { width = 4; height = 2; wrap = 0;"
            " scatter = 0; board = \"..../....\"; notes = \"C4 D4\";"
            " trigger = 1; period = 1 s; hold = 0.5 s; listen = %d;"
            " pass = 0; };\n"
            "  sink { instrument = pad; };\n"
            "};\n";

        char body[1024];

        snprintf(body, sizeof(body), shape, "C4 D4", 1);

        if (notesFrom(plugins, synth, "life-listens", body, 6.0) <= 0)
            fail("a line played into gen::life drew nothing on the board");

        /* C#4 and D#4 are not on a ladder of C4 and D4. A receive that
           snapped to the nearest row would fill this board just as fast
           as the one above, and a piece would have no way to tell a
           mistyped note from a meant one. */
        snprintf(body, sizeof(body), shape, "C#4 D#4", 1);

        if (notesFrom(plugins, synth, "life-ignores-unspellable",
                      body, 6.0) != 0)
            fail("gen::life drew cells for pitches its ladder cannot spell");

        /* And `listen = 0' is the plugin glider.gen has always had. */
        snprintf(body, sizeof(body), shape, "C4 D4", 0);

        if (notesFrom(plugins, synth, "life-listen-0", body, 6.0) != 0)
            fail("gen::life drew on its board with listen = 0");
    }

    /* `pass' is about notes, and a stage that ate anything else would
       be a hole in the pipeline rather than a stage in it.
     *
       The first draft gated the emit on `pass' for every event type, so
       a `pass = 0' board silently swallowed the note-offs, chanarg
       writes and structure edits of every stage upstream of it -- a
       gen::reshape in front of one delivered nothing at all, and the
       piece it was in had a chain that quietly did nothing. Nothing a
       note tape can see, which is why it is asked here directly. */
    {
        const std::string body =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage r gen::reshape { node = \"fmap\"; arg = \"inmax\";"
            " from = 1; to = 0.5; every = 0.5 s; steps = 4; };\n"
            "  stage b gen::life { width = 4; height = 2; wrap = 0;"
            " scatter = 0; board = \"..../....\"; notes = \"C4 D4\";"
            " period = 1 s; hold = 0.5 s; listen = 1; pass = 0; };\n"
            "  sink { instrument = pad; };\n"
            "};\n";

        const std::string path = thUtil::tempFile("gencheck-passthru-");

        if (path.empty())
            fail("could not write the pass-through piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("the pass-through piece did not load");
            else if (render(sched, 6.0, 0.02).find("E ") == std::string::npos)
                fail("a gen::life with pass = 0 swallowed the structure "
                     "edits of the stage in front of it");

            remove(path.c_str());
            clearChannels(synth);
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 4, 5. the chord is counted in degrees ---- */

    {
        clearChannels(synth);
        drainSynth();

        /* A minor pentatonic on A: 45 48 50 52 55, then 57. Two degrees
           above 45 is 50 -- five semitones -- and two above 48 is 52,
           which is four. A fixed-interval shifter cannot tell those
           apart, and telling them apart is the whole plugin. */
        const std::string body =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 2; fills = 2;"
            "    notes = \"A2 C3\"; period = 1 s; hold = 0.5 s; };\n"
            "  stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
            "    voices = 2; step = 2; spread = 0 s; taper = 1; };\n"
            "  sink { instrument = pad; };\n"
            "};\n";

        const std::string path = thUtil::tempFile("gencheck-harm-");

        if (path.empty())
            fail("could not write the harmonize piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("the harmonize piece did not load");
            else
            {
                std::set<int> heard;
                std::istringstream lines(render(sched, 6.0, 0.02));
                std::string line;

                while (std::getline(lines, line))
                {
                    std::istringstream f(line);
                    std::string tag, at, ch, note;

                    if ((f >> tag >> at >> ch >> note) && tag == "N")
                        heard.insert(atoi(note.c_str()));
                }

                /* Roots, and the second degree above each. */
                if (!heard.count(45) || !heard.count(48))
                    fail("harmonize dropped the roots it was given");
                else if (!heard.count(50))
                    fail("harmonize did not stack two degrees above 45");
                else if (!heard.count(52))
                    fail("harmonize did not stack two degrees above 48");
                else if (heard.count(47) || heard.count(51) ||
                         heard.count(53))
                    fail("harmonize emitted a pitch outside its scale");

                /* The two gaps differ, measured rather than asserted.
                 *
                   The first draft of this compared two literals -- 50-45
                   against 52-48 -- which the compiler can answer without
                   running anything, so it was a comment with a shape
                   like a test. What has to be measured is the *gap this
                   plugin produced*, which means finding what it stacked
                   over each root in the stream rather than naming it. */
                int over45 = -1, over48 = -1;

                for (std::set<int>::iterator i = heard.begin();
                     i != heard.end(); ++i)
                {
                    if (*i > 45 && *i < 48 + 12 && over45 < 0 && *i != 48)
                        over45 = *i;

                    if (*i > 48 && over48 < 0)
                        over48 = *i;
                }

                if (over45 < 0 || over48 < 0)
                    fail("harmonize stacked nothing over one of its roots");
                else if (over45 - 45 == over48 - 48)
                    fail("harmonize put the same number of semitones over "
                         "both roots; it is counting semitones, not "
                         "degrees");
            }

            remove(path.c_str());
        }
    }

    clearChannels(synth);
    drainSynth();

    /* `voices = 1' is a passthrough -- the identity every transformer
       should have and the one worth pinning, because it is what a piece
       reaches for when it wants the stage present and doing nothing. */
    {
        const char *shape =
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 2; fills = 2;"
            "    notes = \"A2 C3\"; period = 1 s; hold = 0.5 s; };\n"
            "  %s"
            "  sink { instrument = pad; };\n"
            "};\n";

        struct { const char *what; const char *stage; bool wantRoot;
                 int wantOther; size_t exact; } cases[] = {
            { "voices = 1 was not a passthrough",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 1; };\n", true, -1, 2 },
            /* Not just "45 is there" -- euclid supplies that whatever
               harmonize does. The identity claim is that *nothing else*
               is there, which `exact' below is what checks. */
            { "root = 0 still sounded the note it was given",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 2; step = 2; root = 0; };\n", false, 50, 2 },
            { "below = 1 did not stack downward",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 2; step = 2; below = 1; };\n", true, 40, 4 },
        };

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            char body[2048];

            snprintf(body, sizeof(body), shape, cases[c].stage);

            const std::string path = thUtil::tempFile("gencheck-harm2-");

            if (path.empty())
            {
                fail("could not write a harmonize variant");
                continue;
            }

            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail(std::string("a harmonize variant did not load: ") +
                     cases[c].what);
            else
            {
                std::set<int> heard;
                std::istringstream lines(render(sched, 6.0, 0.02));
                std::string line;

                while (std::getline(lines, line))
                {
                    std::istringstream f(line);
                    std::string tag, at, ch, note;

                    if ((f >> tag >> at >> ch >> note) && tag == "N")
                        heard.insert(atoi(note.c_str()));
                }

                if (heard.count(45) != (cases[c].wantRoot ? 1u : 0u))
                    fail(cases[c].what);
                else if (cases[c].wantOther >= 0 &&
                         !heard.count(cases[c].wantOther))
                    fail(cases[c].what);
                else if (heard.size() != cases[c].exact)
                    fail(std::string(cases[c].what) + " (heard " +
                         std::to_string(heard.size()) + " distinct "
                         "pitches, wanted " +
                         std::to_string(cases[c].exact) + ")");
            }

            remove(path.c_str());
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 5b. every on gets exactly one off ---- */

    /* The half of a transformer that no rendered tape can see.
     *
     * A generator's notes carry their own duration, so the scheduler
     * derives each off from the event it delivered and a transformer
     * that never thinks about offs at all still works. Live input does
     * not: a key arrives as a THC_EV_NOTE with duration 0 and a
     * THC_EV_NOTEOFF whenever the hand lets go, and a transformer that
     * turned one note into five owes five offs, at the right pitches
     * and at the right times. Three ways to get that wrong, all of
     * which this plugin did, all of which leave a note ringing until
     * the program is closed:
     *
     *   - a pitch pressed twice before either release;
     *   - `spread', where each voice has to be released as late as it
     *     was pressed, or the offs arrive before their own ons;
     *   - a press that emitted nothing, whose release must emit nothing
     *     rather than forwarding the root it deliberately dropped.
     *
     * So this drives the chain by hand and counts. Pitch by pitch, and
     * with the times compared, because "the same number of ons and
     * offs" is a gate that a chord released at the wrong pitches walks
     * straight through.
     */
    {
        struct Case
        {
            const char *what;
            const char *stage;
            bool        twice;      /* press it again before releasing */
        };

        static const Case cases[] = {
            { "a held chord",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 3; step = 2; };\n", false },
            { "a held chord pressed twice",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 3; step = 2; };\n", true },
            /* A chord voiced from the one before it: the second
               press is the first one this stage has led, and it has to
               release what it led rather than what it would have
               stacked. */
            { "a led chord pressed twice",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 3; step = 2; lead = 1; };\n", true },
            { "a rolled chord released early",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 3; step = 2; spread = 0.5 s; };\n", false },
            { "a chord whose root was dropped",
              "stage h xform::harmonize { scale = \"A2 C3 D3 E3 G3\";"
              " voices = 1; root = 0; };\n", false },
        };

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            char body[1024];

            snprintf(body, sizeof(body),
                     "chain hands {\n"
                     "  input midi;\n"
                     "  %s"
                     "  sink { channel = 1; };\n"
                     "};\n", cases[c].stage);

            const std::string path = thUtil::tempFile("gencheck-held-");

            if (path.empty())
            {
                fail("could not write the held-note piece");
                continue;
            }

            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << body;
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail(std::string("the held-note piece did not load: ") +
                     cases[c].what);
            else
            {
                /* pitch -> (ons, offs), and the time of each. */
                std::map<int, std::vector<double> > ons, offs;

                sigc::connection conn = sched.sigDelivered.connect(
                    [&ons, &offs](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NOTE)
                            ons[ev.u.note.note].push_back(ev.at);
                        else if (ev.type == THC_EV_NOTEOFF)
                            offs[ev.u.note.note].push_back(ev.at);
                    });

                sched.start();

                thcEvent key = {};

                key.type = THC_EV_NOTE;
                key.u.note.level = 1;
                key.channel = 0;
                key.u.note.note = 45;
                key.u.note.velocity = 90;
                key.u.note.duration = 0;
                key.at = sched.now();

                sched.injectMidiEvent(key);

                if (cases[c].twice)
                {
                    sched.stepTransport(0.1);

                    key.at = sched.now();
                    sched.injectMidiEvent(key);
                }

                /* Let go well inside the roll, which is what makes the
                   spread case a question at all. */
                sched.stepTransport(0.15);

                thcEvent up = {};

                up.type = THC_EV_NOTEOFF;
                up.channel = 0;
                up.u.note.note = 45;
                up.at = sched.now();

                sched.injectMidiEvent(up);

                if (cases[c].twice)
                    sched.injectMidiEvent(up);

                /* Long enough for a 0.5s roll's last voice and its off. */
                for (int i = 0; i < 200; i++)
                    sched.stepTransport(0.02);

                conn.disconnect();

                for (std::map<int, std::vector<double> >::iterator
                         i = ons.begin(); i != ons.end(); ++i)
                {
                    const size_t up2 = i->second.size();
                    const size_t down = offs.count(i->first)
                        ? offs[i->first].size() : 0;

                    if (up2 != down)
                    {
                        fail(std::string(cases[c].what) + ": pitch " +
                             std::to_string(i->first) + " sounded " +
                             std::to_string(up2) + " times and was "
                             "released " + std::to_string(down));
                        break;
                    }

                    /* And each release after the press it answers. An
                       off ahead of its own on is a note that never
                       stops. */
                    bool bad = false;

                    for (size_t k = 0; k < up2; k++)
                        if (offs[i->first][k] < i->second[k])
                            bad = true;

                    if (bad)
                    {
                        fail(std::string(cases[c].what) + ": pitch " +
                             std::to_string(i->first) + " was released "
                             "before it sounded");
                        break;
                    }
                }

                /* And nothing released that never sounded. */
                for (std::map<int, std::vector<double> >::iterator
                         i = offs.begin(); i != offs.end(); ++i)
                    if (!ons.count(i->first))
                    {
                        fail(std::string(cases[c].what) + ": pitch " +
                             std::to_string(i->first) + " was released "
                             "but never sounded");
                        break;
                    }
            }

            remove(path.c_str());
        }
    }

    clearChannels(synth);
    drainSynth();

    /* ---- 6. and the whole thing replays ---- */

    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(piece, &sched))
            fail("colony.gen did not load for the replay check");
        else
        {
            const std::string first = render(sched, 100.0, 0.04);

            sched.reset();

            const std::string second = render(sched, 100.0, 0.04);

            if (first.empty())
                fail("colony.gen delivered nothing");
            else if (first != second)
                fail("colony.gen did not replay; a learner or a board is "
                     "carrying state across a rewind");
        }
    }

    clearChannels(synth);
}

/* ---- 8. every shipped piece still loads -------------------------------- */

/* ---- the phrasing plugins ---------------------------------------------- */

/* A note off the tape. */
struct Heard
{
    double at;
    int    channel, note, vel;
    double dur, level;
};

static std::vector<Heard>
notesOf (const std::string &tape)
{
    std::vector<Heard> out;
    std::istringstream lines(tape);
    std::string line;

    while (std::getline(lines, line))
    {
        std::istringstream f(line);
        std::string tag;
        Heard h;

        if ((f >> tag >> h.at >> h.channel >> h.note >> h.vel >> h.dur >>
              h.level) &&
            tag == "N")
            out.push_back(h);
    }

    return out;
}

/* Write `body' out, load it, render `seconds' of it, and hand back the
 * notes. An empty result with `what' in the failure is a piece that did
 * not load. */
static std::vector<Heard>
playBody (const std::map<std::string, thcPlugin *> &plugins, thSynth *synth,
          const char *what, const std::string &body, double seconds)
{
    std::vector<Heard> none;
    const std::string path = thUtil::tempFile("gencheck-kit-");

    if (path.empty())
    {
        fail(std::string("could not write the ") + what + " piece");
        return none;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    clearChannels(synth);
    drainSynth();

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(path, &sched))
    {
        for (size_t k = 0; k < loader.errors().size(); k++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[k].c_str());

        fail(std::string("the ") + what + " piece did not load");
        remove(path.c_str());
        return none;
    }

    std::vector<Heard> heard = notesOf(render(sched, seconds, 0.02));

    remove(path.c_str());
    return heard;
}

static bool
near (double a, double b)
{
    return std::fabs(a - b) < 1e-6;
}

/* Ties and accents in a grammar, a rest in a pool, and the transformers
 * that shape a phrase: form, swing, echo, chance, ratchet. Each is the
 * arithmetic a piece leans on, measured off the tape. */
static void
checkPhrasing (const std::map<std::string, thcPlugin *> &plugins,
               thSynth *synth)
{
    {
        const char *need[] = { "lsystem", "euclid", "form", "swing", "echo",
                               "chance", "ratchet", "level", NULL };

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                return;
            }
    }

    /* A tie lengthens the note before it and takes a step; an accent
       mark moves the next note's velocity; the phrase is as long as its
       steps and repeats on the step after the last. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "ties",
            "chain c {\n"
            "  stage src gen::lsystem { axiom = \">F__r<<F\"; rules = \"\";"
            "    depth = 0; notes = \"C4\"; step = 1 s; hold = 0.5 s;"
            "    vel = 80; accent = 10; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 9.5);

        if (h.size() != 4)
            fail("ties: expected 4 notes in 9.5 s, got " +
                 std::to_string(h.size()));
        else
        {
            if (!near(h[0].at, 0) || !near(h[0].dur, 2.5) || h[0].vel != 90)
                fail("ties: `>F__' should be one note at 0, 2.5 s long, "
                     "at velocity 90");

            if (!near(h[1].at, 4) || !near(h[1].dur, 0.5) || h[1].vel != 60)
                fail("ties: `<<F' after a rest should land at 4 s, 0.5 s "
                     "long, at velocity 60");

            if (!near(h[2].at, 5))
                fail("ties: a five-step phrase should repeat at 5 s, not "
                     "at " + std::to_string(h[2].at));
        }
    }

    /* A rest in a cycled pool takes an onset and sounds nothing. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "pool rest",
            "chain c {\n"
            "  stage src gen::euclid { steps = 3; fills = 3;"
            "    notes = \"C4 . E4\"; period = 1 s; hold = 0.5 s; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 2.5);

        if (h.size() != 2 || h[0].note != 60 || h[1].note != 64 ||
            !near(h[1].at, 2))
            fail("pool rest: `C4 . E4' should sound C4 at 0 and E4 at 2, "
                 "with the rest taking its turn between");
    }

    /* form: the pattern is bars, counted from zero, and a note in a
       resting bar is dropped. Two notes a bar, `x.' over one-second
       bars: the first, third and fifth seconds sound. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "form",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 0.5 s; hold = 0.2 s; };\n"
            "  stage f xform::form { pattern = \"x.\"; bar = 1 s; mode = 0; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 5.9);

        bool ok = h.size() == 6;

        for (size_t i = 0; ok && i < h.size(); i++)
            if ((long)floor(h[i].at + 1e-6) % 2 != 0)
                ok = false;

        if (!ok)
            fail("form: `x.' over 1 s bars should pass exactly the even "
                 "seconds' notes; got " + std::to_string(h.size()));

        /* mode 1: the pattern once, then everything. */
        h = playBody(plugins, synth, "form intro",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 1 s; hold = 0.2 s; };\n"
            "  stage f xform::form { pattern = \"..\"; bar = 1 s; mode = 1; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 4.5);

        if (h.size() != 3 || !near(h[0].at, 2))
            fail("form: `..' with mode 1 should rest two bars and then "
                 "play on");
    }

    /* swing: odd divisions of the grid move later by a third of a
       division at amount 1; even ones stay. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "swing",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 0.25 s; hold = 0.1 s; };\n"
            "  stage s xform::swing { grid = 0.25 s; amount = 1; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 0.9);

        if (h.size() != 4)
            fail("swing: expected 4 notes, got " + std::to_string(h.size()));
        else if (!near(h[0].at, 0) || !near(h[2].at, 0.5))
            fail("swing: the onbeats moved");
        else if (!near(h[1].at, 0.25 + 0.25 / 3) ||
                 !near(h[3].at, 0.75 + 0.25 / 3))
            fail("swing: the offbeats did not move a third of a division");
    }

    /* echo: repeats `time' apart, each `decay' as loud, and with `pass'
       off the note itself is not heard. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "echo",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 10 s; hold = 0.1 s; vel = 100; };\n"
            "  stage e xform::echo { repeats = 2; time = 0.5 s; decay = 0.5;"
            "    shift = 12; pass = 0; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 5);

        if (h.size() != 2)
            fail("echo: two repeats with pass off should be two notes, got "
                 + std::to_string(h.size()));
        else if (!near(h[0].at, 0.5) || h[0].vel != 50 || h[0].note != 72 ||
                 !near(h[1].at, 1.0) || h[1].vel != 25 || h[1].note != 84)
            fail("echo: repeats should land at 0.5 and 1.0, at 50 and 25, "
                 "an octave up each time");
    }

    /* chance: at 0 nothing passes, at 1 everything does, and in between
       the same seed drops the same notes twice. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "chance",
            "seed 5;\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 0.1 s; hold = 0.05 s; };\n"
            "  stage g xform::chance { prob = 0; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 2);

        if (!h.empty())
            fail("chance: prob 0 let a note through");

        h = playBody(plugins, synth, "chance",
            "seed 5;\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 0.1 s; hold = 0.05 s; };\n"
            "  stage g xform::chance { prob = 1; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 1.95);

        if (h.size() != 20)
            fail("chance: prob 1 should pass all 20 notes, passed " +
                 std::to_string(h.size()));

        const std::string half =
            "seed 5;\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 0.1 s; hold = 0.05 s; };\n"
            "  stage g xform::chance { prob = 0.5; };\n"
            "  sink { channel = 1; };\n"
            "};\n";

        std::vector<Heard> a = playBody(plugins, synth, "chance", half, 3.95);
        std::vector<Heard> b = playBody(plugins, synth, "chance", half, 3.95);

        if (a.empty() || a.size() == 40)
            fail("chance: prob 0.5 over 40 notes kept " +
                 std::to_string(a.size()) + ", which is not a coin");
        else if (a.size() != b.size())
            fail("chance: the same seed kept a different number of notes "
                 "the second time");
        else
            for (size_t i = 0; i < a.size(); i++)
                if (!near(a[i].at, b[i].at))
                {
                    fail("chance: the same seed dropped different notes "
                         "the second time");
                    break;
                }
    }

    /* A fader changes the note's gain without changing how it is played. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "level",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 10 s; hold = 0.1 s; vel = 100; };\n"
            "  stage l xform::level { gain = 0.6; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 5);

        if (h.size() != 1 || h[0].vel != 100 || !near(h[0].level, 0.6))
            fail("level: gain 0.6 should leave velocity 100 and set level 0.6");
    }

    /* ratchet: a burst is `count' notes across the note's own duration,
       so it is as long as what it replaced. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "ratchet",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 10 s; hold = 1 s; vel = 100; };\n"
            "  stage r xform::ratchet { count = 4; prob = 1; decay = 0.5; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 5);

        if (h.size() != 4)
            fail("ratchet: one note at prob 1, count 4 should be 4 notes, "
                 "got " + std::to_string(h.size()));
        else
        {
            double span = 0;

            for (size_t i = 0; i < h.size(); i++)
            {
                if (!near(h[i].at, i * 0.25) || !near(h[i].dur, 0.25))
                    fail("ratchet: the burst is not evenly across the note");

                span += h[i].dur;
            }

            if (!near(span, 1) || h[1].vel != 50 || h[3].vel != 13)
                fail("ratchet: the burst should be as long as the note "
                     "and decay by half each time");
        }
    }
}

/* ---- the harmony plugins ------------------------------------------------ */

/* progression walks a key with a cadence at every phrase end, two stages
 * with one `seed' walk together, bassline plays a pattern under a root
 * in degrees of the scale, and counterpoint keeps to the scale, to
 * consonances, and off parallel perfects. */
static void
checkHarmonyKit (const std::map<std::string, thcPlugin *> &plugins,
                 thSynth *synth)
{
    {
        const char *need[] = { "progression", "bassline", "counterpoint",
                               "euclid", NULL };

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                return;
            }
    }

    /* C major from C3: 48 50 52 53 55 57 59. Phrases of four: chords 0
       and 4 are the tonic, chord 3 the dominant, and everything is in
       the pool. Two chains with the same seed agree note for note. */
    {
        const std::string body =
            "seed 11;\n"
            "chain a {\n"
            "  stage src gen::progression { scale = \"C3 D3 E3 F3 G3 A3 B3\";"
            "    every = 1 s; hold = 0.9 s; phrase = 4; wander = 0.2;"
            "    seed = 42; };\n"
            "  sink { channel = 1; };\n"
            "};\n"
            "chain b {\n"
            "  stage src gen::progression { scale = \"C3 D3 E3 F3 G3 A3 B3\";"
            "    every = 1 s; hold = 0.9 s; phrase = 4; wander = 0.2;"
            "    seed = 42; };\n"
            "  sink { channel = 2; };\n"
            "};\n";

        std::vector<Heard> h = playBody(plugins, synth, "progression",
                                        body, 15.5);
        std::vector<Heard> a, b;

        for (size_t i = 0; i < h.size(); i++)
            (h[i].channel == 0 ? a : b).push_back(h[i]);

        if (a.size() != 16 || b.size() != 16)
            fail("progression: expected 16 chords on each of two chains, "
                 "got " + std::to_string(a.size()) + " and " +
                 std::to_string(b.size()));
        else
        {
            static const int pool[7] = { 48, 50, 52, 53, 55, 57, 59 };

            for (size_t i = 0; i < a.size(); i++)
            {
                bool inPool = false;

                for (int k = 0; k < 7; k++)
                    if (a[i].note == pool[k])
                        inPool = true;

                if (!inPool)
                    fail("progression: a root outside the key");

                if (i % 4 == 0 && a[i].note != 48)
                    fail("progression: a phrase that does not open on the "
                         "tonic");

                if (i % 4 == 3 && a[i].note != 55)
                    fail("progression: a phrase that does not close on the "
                         "dominant");

                if (a[i].note != b[i].note)
                    fail("progression: two stages with one seed walked "
                         "different chords");
            }
        }
    }

    /* bassline: `rtfo' under C3 with octave -1 is C2 E2 G2 C3, one step
       apart, and the root itself is not heard with pass off; a tie is
       one longer note. In A minor the third under A is minor. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "bassline",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C3\"; period = 10 s; hold = 2 s; };\n"
            "  stage b xform::bassline { scale = \"C3 D3 E3 F3 G3 A3 B3\";"
            "    pattern = \"rtfo_\"; step = 0.25 s; hold = 0.2 s;"
            "    octave = -1; pass = 0; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 5);

        if (h.size() != 4)
            fail("bassline: `rtfo_' should be four notes, got " +
                 std::to_string(h.size()));
        else if (h[0].note != 36 || h[1].note != 40 || h[2].note != 43 ||
                 h[3].note != 48)
            fail("bassline: `rtfo' under C3 an octave down is not C2 E2 "
                 "G2 C3");
        else if (!near(h[1].at, 0.25) || !near(h[3].dur, 0.45))
            fail("bassline: steps are not `step' apart, or the tie did "
                 "not lengthen the octave");

        h = playBody(plugins, synth, "bassline minor",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"A2\"; period = 10 s; hold = 2 s; };\n"
            "  stage b xform::bassline { scale = \"A2 B2 C3 D3 E3 F3 G3\";"
            "    pattern = \"t\"; step = 0.25 s; hold = 0.2 s;"
            "    octave = 0; pass = 0; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 5);

        if (h.size() != 1 || h[0].note != 48)
            fail("bassline: the third over A in A minor should be C, the "
                 "scale's third and not a major one");
    }

    /* counterpoint: under a C major line every added note is in the key
       and at a consonance, the first is a perfect one, and no two
       successive pairs are parallel fifths or octaves. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "counterpoint",
            "chain c {\n"
            "  stage src gen::euclid { steps = 8; fills = 8;"
            "    notes = \"C4 D4 E4 F4 G4 A4 G4 E4\"; period = 0.5 s;"
            "    hold = 0.4 s; vel = 100; };\n"
            "  stage k xform::counterpoint { scale = \"C3 D3 E3 F3 G3 A3 B3\";"
            "    below = 1; taper = 0.5; pass = 1; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 3.9);

        if (h.size() != 16)
            fail("counterpoint: eight melody notes should be sixteen on the "
                 "tape, got " + std::to_string(h.size()));
        else
        {
            static const bool inC[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 };
            int lastM = -1, lastC = -1;

            for (size_t i = 0; i + 1 < h.size(); i += 2)
            {
                const Heard &m = h[i].vel == 100 ? h[i] : h[i + 1];
                const Heard &c = h[i].vel == 100 ? h[i + 1] : h[i];
                const int iv = m.note - c.note;

                if (c.vel != 50)
                    fail("counterpoint: the added voice is not tapered");

                if (!inC[c.note % 12])
                    fail("counterpoint: an added note outside the scale");

                if (iv <= 0 || (iv % 12 != 0 && iv % 12 != 3 &&
                                iv % 12 != 4 && iv % 12 != 7 &&
                                iv % 12 != 8 && iv % 12 != 9))
                    fail("counterpoint: an added note at a dissonance, or "
                         "above the melody");

                if (i == 0 && iv % 12 != 0 && iv % 12 != 7)
                    fail("counterpoint: the first interval is not perfect");

                if (lastM >= 0)
                {
                    const int prev = lastM - lastC;
                    const bool perf = iv % 12 == 0 || iv % 12 == 7;
                    const bool prevPerf = prev % 12 == 0 || prev % 12 == 7;

                    if (perf && prevPerf && m.note != lastM &&
                        c.note != lastC &&
                        (m.note > lastM) == (c.note > lastC))
                        fail("counterpoint: parallel perfect intervals");
                }

                lastM = m.note;
                lastC = c.note;
            }
        }
    }
}

/* ---- held notes through the transformers ------------------------------ */

/* An off must go where its on went, and every stage that can drop, delay
 * or replace a note owes that. None of the three rules below is obvious,
 * each was got wrong once, and none of them is visible in a rendered
 * piece -- a stuck note is a piece that sounds slightly wrong an hour in.
 * So each is played by hand here, in virtual time, live input being the
 * only thing that asks the question at all.
 */

struct Touch
{
    double at;
    int    note, vel;
    double dur;                 /* 0: a key held down                    */
    bool   on;
};

struct Sounded
{
    double at;
    int    channel, note;
    double dur;
    bool   on;
};

/* Load `body', play `hand' into it, and hand back every note and off the
 * scheduler delivered -- offs included, which is what separates this from
 * playBody: the whole question here is which offs come out. */
static std::vector<Sounded>
playHand (const std::map<std::string, thcPlugin *> &plugins, thSynth *synth,
          const char *what, const std::string &body,
          const std::vector<Touch> &hand, double seconds)
{
    std::vector<Sounded> out;
    const std::string path = thUtil::tempFile("gencheck-held-");

    if (path.empty())
    {
        fail(std::string("could not write the ") + what + " piece");
        return out;
    }

    {
        std::ofstream f(path.c_str(), std::ios::trunc);

        f << body;
    }

    clearChannels(synth);
    drainSynth();

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(path, &sched))
    {
        for (size_t k = 0; k < loader.errors().size(); k++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[k].c_str());

        fail(std::string("the ") + what + " piece did not load");
        remove(path.c_str());
        return out;
    }

    sigc::connection conn = sched.sigDelivered.connect(
        [&out](const thcEvent &ev)
        {
            if (ev.type != THC_EV_NOTE && ev.type != THC_EV_NOTEOFF)
                return;

            Sounded s;

            s.at = ev.at;
            s.channel = ev.channel;
            s.note = ev.u.note.note;
            s.dur = ev.u.note.duration;
            s.on = (ev.type == THC_EV_NOTE);

            out.push_back(s);
        });

    sched.start();

    size_t next = 0;

    while (sched.now() < seconds)
    {
        sched.stepTransport(0.02);

        /* `<=', and the whole backlog each step: a hand written at 0.30
           on a 0.02 clock must not fall between two steps. */
        while (next < hand.size() && hand[next].at <= sched.now())
        {
            thcEvent ev = {};

            ev.type = hand[next].on ? THC_EV_NOTE : THC_EV_NOTEOFF;
            ev.u.note.level = 1;
            ev.at = sched.now();
            ev.channel = 0;
            ev.u.note.note = hand[next].note;
            ev.u.note.velocity = hand[next].vel;
            ev.u.note.duration = hand[next].dur;

            sched.injectMidiEvent(ev);
            next++;
        }
    }

    sched.stop();
    conn.disconnect();
    drainSynth();

    remove(path.c_str());

    if (next != hand.size())
        fail(std::string(what) + ": the hand did not finish inside the "
             "render; the piece is shorter than the part");

    return out;
}

static int
countOff (const std::vector<Sounded> &heard)
{
    int n = 0;

    for (size_t i = 0; i < heard.size(); i++)
        if (!heard[i].on)
            n++;

    return n;
}

static void
checkHeldNotes (const std::map<std::string, thcPlugin *> &plugins,
                thSynth *synth)
{
    {
        const char *need[] = { "form", "echo", "counterpoint", NULL };

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                return;
            }
    }

    /* xform::form. Bar 0 plays, bar 1 rests. A held key and a note
       carrying its own duration both go through in bar 0; the duration
       one never sends its off back this way, so a stage that counted it
       would be holding a tally for pitch 60 that nothing spends. Bar 1's
       press is dropped, and the release that follows it must be dropped
       with it -- spending that tally instead is an off downstream for a
       pitch this stage is not holding, which takes somebody else's note
       down. */
    {
        const std::string body =
            "chain g {\n"
            "    input midi;\n"
            "    stage f xform::form { pattern = \"x.\"; bar = 1 s;"
            "        mode = 0; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        std::vector<Touch> hand;
        const Touch part[] = {
            { 0.10, 60, 90, 0.0, true  },   /* held, in a playing bar    */
            { 0.30, 60, 90, 0.2, true  },   /* and one with a duration   */
            { 0.50, 60,  0, 0.0, false },   /* the held one comes up     */
            { 1.10, 60, 90, 0.0, true  },   /* dropped: a resting bar    */
            { 1.30, 60,  0, 0.0, false },   /* and so is this            */
        };

        for (size_t i = 0; i < sizeof(part) / sizeof(part[0]); i++)
            hand.push_back(part[i]);

        const std::vector<Sounded> heard =
            playHand(plugins, synth, "form", body, hand, 2.0);

        int on = 0;

        for (size_t i = 0; i < heard.size(); i++)
            if (heard[i].on)
                on++;

        if (on != 2)
            fail("form: expected the two notes in the playing bar, got " +
                 std::to_string(on));

        if (countOff(heard) != 1)
            fail("form: expected one off -- the held note's -- and got " +
                 std::to_string(countOff(heard)) +
                 "; a dropped note's release was forwarded");
    }

    /* xform::counterpoint against a scale of one pitch class. Nothing is
       a third, fifth, sixth or octave from D in a scale of nothing but C,
       so the stage puts up nothing, and what puts up nothing takes down
       nothing. The marker for "nothing" used to be -1 and used to go out
       as a note number. `pass' is off, so anything at all on this tape
       came from the stage itself. */
    {
        const std::string body =
            "chain c {\n"
            "    input midi;\n"
            "    stage v xform::counterpoint { scale = \"C3\"; below = 1;"
            "        pass = 0; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        std::vector<Touch> hand;
        const Touch part[] = {
            { 0.10, 62, 90, 0.0, true  },
            { 0.50, 62,  0, 0.0, false },
        };

        for (size_t i = 0; i < sizeof(part) / sizeof(part[0]); i++)
            hand.push_back(part[i]);

        const std::vector<Sounded> heard =
            playHand(plugins, synth, "counterpoint", body, hand, 1.0);

        for (size_t i = 0; i < heard.size(); i++)
            if (heard[i].note < 0 || heard[i].note > 127)
                fail("counterpoint: delivered note " +
                     std::to_string(heard[i].note) +
                     ", which is not a pitch");

        if (!heard.empty())
            fail("counterpoint: a root the scale had no consonance for "
                 "still put " + std::to_string(heard.size()) +
                 " event(s) out");
    }

    /* xform::echo, and a key pressed twice before it comes up. Two
       presses make two sets of repeats and only one release is ever
       coming, so the first set has to come down as the second goes up --
       or it never comes down at all. `pass' is off, so the count is the
       echoes' own. */
    {
        const std::string body =
            "chain e {\n"
            "    input midi;\n"
            "    stage r xform::echo { repeats = 2; time = 0.1 s;"
            "        decay = 0.9; shift = 0; pass = 0; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        std::vector<Touch> hand;
        const Touch part[] = {
            { 0.10, 60, 100, 0.0, true  },
            { 0.40, 60, 100, 0.0, true  },   /* again, without letting go */
            { 0.80, 60,   0, 0.0, false },
        };

        for (size_t i = 0; i < sizeof(part) / sizeof(part[0]); i++)
            hand.push_back(part[i]);

        const std::vector<Sounded> heard =
            playHand(plugins, synth, "echo", body, hand, 2.0);

        int on = 0;

        for (size_t i = 0; i < heard.size(); i++)
            if (heard[i].on)
                on++;

        if (on != 4)
            fail("echo: two presses at two repeats each is four echoes, "
                 "got " + std::to_string(on));
        else if (countOff(heard) != on)
            fail("echo: " + std::to_string(on) + " echoes went up and " +
                 std::to_string(countOff(heard)) +
                 " came down; the first press's repeats are stuck");
    }

    /* xform::chance is xform::form's bookkeeping with a die in front of
       it, so it is pinned by the rule rather than by a count: which notes
       a seeded gate keeps is its business, but an off it lets through for
       a press it dropped is nobody's. Only held ons are counted -- one
       carrying a duration releases itself downstream and never appears
       here as an off -- so the running balance for a pitch is the number
       of keys this stage is holding down, and it cannot go negative. */
    {
        const std::string body =
            "seed 7;\n"
            "chain d {\n"
            "    input midi;\n"
            "    stage g xform::chance { prob = 0.5; };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        std::vector<Touch> hand;

        for (int i = 0; i < 8; i++)
        {
            const double t = 0.1 + i * 0.4;
            const Touch bar[] = {
                { t,        60, 90, 0.2, true  },  /* releases itself    */
                { t + 0.15, 60, 90, 0.0, true  },  /* a key held down    */
                { t + 0.30, 60,  0, 0.0, false },  /* and let go         */
            };

            for (size_t k = 0; k < sizeof(bar) / sizeof(bar[0]); k++)
                hand.push_back(bar[k]);
        }

        const std::vector<Sounded> heard =
            playHand(plugins, synth, "chance", body, hand, 4.0);

        if (heard.empty())
            fail("chance: a gate at even odds let nothing through in "
                 "twenty-four events; the piece is not playing");

        int holding = 0;

        for (size_t i = 0; i < heard.size(); i++)
        {
            if (heard[i].on)
            {
                if (heard[i].dur <= 0)
                    holding++;
            }
            else if (--holding < 0)
            {
                fail("chance: an off came through for a press that did "
                     "not, at " + std::to_string(heard[i].at) + " s");
                break;
            }
        }
    }

    /* Not a held note, but the same shape of mistake: a value that is not
       a pitch reaching a sink as one. docs/GEN_FORMAT.md lets a `.' into any
       note list and rests the whole idea on every ladder filtering what
       it is handed to 0..127. gen::life climbs a ladder and was the one
       that did not, so a rest in its scale played note -1 on row 0 and
       notes 11, 23, 35 above it -- a wrong pitch rather than a silence.
       Checked here rather than in a shipped piece because no shipped
       piece writes a rest into a life ladder, which is exactly why it
       went unnoticed. */
    if (plugins.find("life") == plugins.end())
        fail("module 'life' is missing; build the plugins first");
    else
    {
        const std::string body =
            "scale gapped \"C3 . E3 . G3\";\n"
            "chain b {\n"
            "    stage board gen::life {\n"
            "        board = \"............/"
            "............/............/....OOO...../"
            "............/............\";\n"
            "        width = 12; height = 6;\n"
            "        scatter = 0; trigger = 0; wrap = 1;\n"
            "        notes = gapped;\n"
            "        period = 0.1 s; hold = 0.05 s; vel = 84;\n"
            "    };\n"
            "    sink { channel = 1; };\n"
            "};\n";

        const std::vector<Heard> heard =
            playBody(plugins, synth, "life-rest", body, 4.0);

        if (heard.empty())
            fail("life: a blinker on a gapped ladder played nothing");

        for (size_t i = 0; i < heard.size(); i++)
            if (heard[i].note < 0 || heard[i].note > 127)
            {
                fail("life: a rest in the ladder played note " +
                     std::to_string(heard[i].note) +
                     ", which is not a pitch");
                break;
            }
    }
}

/* The corpus instinct, applied to .gen.
 *
 * Everything above builds its own files or leans on the one piece passed
 * in, so the other shipped pieces were gated by nothing at all: a param
 * renamed in a plugin, a unit tightened in the loader, a knob whose
 * metadata stopped being accepted, and fern.gen or loom.gen would have
 * quietly stopped loading with no test anywhere to say so. dspcheck has
 * swept dsp/ for exactly this reason since long before any of this.
 *
 * Loading is most of it, and one thing more: a piece that loads and then
 * says nothing is a piece with a typo in it, and the demos are meant to
 * be read as much as heard. So every piece with a generator in it has to
 * deliver something inside a minute of its own virtual time. A piece
 * whose chains are all `input midi' is exempt, because silence is
 * exactly what it should produce with nobody playing -- hands.gen is
 * that piece, and the exemption is why the check can be strict about
 * everything else.
 *
 * Replay determinism is not swept here: it needs a pinned seed and three
 * minutes, and one piece carrying that is enough.
 */
/* Every .gen beside the one the harness was handed, or nothing when
 * there is no directory to sweep, which is not a failure. Sorted so a
 * failure names the same file on every machine; the directory order is
 * the filesystem's business, not the test's. */
static std::vector<std::filesystem::path>
piecesBeside (const std::string &genFile)
{
    std::vector<std::filesystem::path> files;

    const std::filesystem::path dir =
        std::filesystem::path(genFile).parent_path();

    std::error_code ec;

    if (dir.empty() || !std::filesystem::is_directory(dir, ec))
        return files;

    for (const auto &e : std::filesystem::directory_iterator(dir, ec))
    {
        if (ec)
            break;

        if (e.path().extension() == ".gen")
            files.push_back(e.path());
    }

    std::sort(files.begin(), files.end());

    return files;
}

/* ---- the floor: rows, ducks and transpositions ------------------------ */

/* A chanarg off the tape. */
struct Knob
{
    double at;
    int    channel;
    double value;
};

static std::vector<Knob>
knobsOf (const std::string &tape)
{
    std::vector<Knob> out;
    std::istringstream lines(tape);
    std::string line;

    while (std::getline(lines, line))
    {
        std::istringstream f(line);
        std::string tag, name;
        Knob k;

        if ((f >> tag >> k.at >> k.channel >> name >> k.value) && tag == "C")
            out.push_back(k);
    }

    return out;
}

static std::vector<Knob>
turnBody (const std::map<std::string, thcPlugin *> &plugins, thSynth *synth,
          const char *what, const std::string &body, double seconds)
{
    std::vector<Knob> none;
    const std::string path = thUtil::tempFile("gencheck-floor-");

    if (path.empty())
    {
        fail(std::string("could not write the ") + what + " piece");
        return none;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);

        out << body;
    }

    clearChannels(synth);
    drainSynth();

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(path, &sched))
    {
        for (size_t k = 0; k < loader.errors().size(); k++)
            fprintf(stderr, "gencheck: %s\n", loader.errors()[k].c_str());

        fail(std::string("the ") + what + " piece did not load");
        remove(path.c_str());
        return none;
    }

    std::vector<Knob> heard = knobsOf(render(sched, seconds, 0.02));

    remove(path.c_str());
    return heard;
}

/* gen::steps maps a row onto a range and holds on `_'; gen::pump dips a
 * knob on the beat and comes back; xform::transpose moves a note and its
 * release by the same amount. */
static void
checkFloor (const std::map<std::string, thcPlugin *> &plugins,
            thSynth *synth)
{
    {
        const char *need[] = { "steps", "pump", "transpose", "euclid", NULL };

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                return;
            }
    }

    /* "1 0 _ 0.5" over 10..20: 20 at 0, 10 at 0.25, nothing at 0.5, 15 at
       0.75, and the row round again at 1. */
    {
        std::vector<Knob> k = turnBody(plugins, synth, "steps",
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::steps { values = \"1 0 _ 0.5\";"
            "    period = 0.25 s; min = 10; max = 20; };\n"
            "  sink { instrument = pad; chanarg = \"fmin\"; };\n"
            "};\n", 1.2);

        /* Three values a row -- the hold sends nothing -- at 0, 0.25 and
           0.75, and the row round again at 1.0: four inside 1.2 s. */
        if (k.size() != 4)
            fail("steps: a four-step row with one hold should send three "
                 "values a row, four inside 1.2 s; sent " +
                 std::to_string(k.size()));
        else if (!near(k[0].value, 20) || !near(k[1].value, 10) ||
                 !near(k[1].at, 0.25) || !near(k[2].value, 15) ||
                 !near(k[2].at, 0.75) || !near(k[3].at, 1.0) ||
                 !near(k[3].value, 20))
            fail("steps: the row did not map onto min..max in time");
    }

    /* A duck: the first value of a cycle is level * (1 - depth), the last
       is back at level, and the values never fall between. */
    {
        std::vector<Knob> k = turnBody(plugins, synth, "pump",
            "instrument pad { dsp \"amb01.dsp\"; };\n"
            "chain c {\n"
            "  stage src gen::pump { period = 0.5 s; depth = 0.5;"
            "    hold = 0.05 s; rise = 0.3 s; level = 40; steps = 10;"
            "    curve = 1; };\n"
            "  sink { instrument = pad; chanarg = \"amp\"; };\n"
            "};\n", 0.95);

        if (k.size() != 20)
            fail("pump: ten steps a cycle for two cycles should be twenty "
                 "values; got " + std::to_string(k.size()));
        else
        {
            if (!near(k[0].value, 20) || !near(k[9].value, 40) ||
                !near(k[10].value, 20) || !near(k[10].at, 0.5))
                fail("pump: a cycle should start at level * (1 - depth) "
                     "and end at level, then start again");

            for (size_t i = 1; i < 10; i++)
                if (k[i].value < k[i - 1].value - 1e-6)
                    fail("pump: the way back up went down");
        }
    }

    /* A transposition moves the note; one pushed off the keyboard is
       dropped rather than folded. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "transpose",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"C4\"; period = 1 s; hold = 0.5 s; };\n"
            "  stage t xform::transpose { semitones = 7; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 1.5);

        if (h.size() != 2 || h[0].note != 67 || h[1].note != 67)
            fail("transpose: C4 up seven should be G4");

        h = playBody(plugins, synth, "transpose off the top",
            "chain c {\n"
            "  stage src gen::euclid { steps = 1; fills = 1;"
            "    notes = \"G9\"; period = 1 s; hold = 0.5 s; };\n"
            "  stage t xform::transpose { semitones = 12; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 1.5);

        if (!h.empty())
            fail("transpose: a note pushed off the keyboard should be "
                 "dropped, not folded");
    }
}


/* The arrangement's own lines, for the claim that an edit aimed
   elsewhere leaves them byte for byte as they were. */
static std::vector<std::string>
sectionLines (const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line))
        if (line.compare(0, 7, "section") == 0)
            out.push_back(line);

    return out;
}

/* ---- the arrangement (docs/GEN_FORMAT.md 5c) --------------------------------
 *
 * A section is the piece's shape written once, in the order it is played,
 * instead of an xform::form pattern under every chain. What is checked is
 * what it claims: which chains are heard when, that a level scales the
 * voice gain rather than velocity, that `section end' stops the
 * transport where it says, that a name no chain answers to is caught at
 * load, and that an editor's splices leave the arrangement's bytes alone.
 */
static void
checkSections (const std::map<std::string, thcPlugin *> &plugins,
               thSynth *synth)
{
    if (plugins.find("euclid") == plugins.end())
    {
        fail("module 'euclid' is missing; build the plugins first");
        return;
    }

    /* Two chains a beat apart at 120, four beats to a bar: one bar is two
       seconds and holds four notes of each. Three bars, each doing
       something different to them. */
    const std::string body =
        "tempo 120;\n"
        "meter 4;\n"
        "seed 5;\n"
        "section one   1 bars { snare = 0; };\n"
        "section two   1 bars { kick = 0; };\n"
        "section three 1 bars { kick = 0.5; snare = 0; };\n"
        "chain kick {\n"
        "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
        "    notes = \"C2\"; period = 1 beats; hold = 0.2 beats;\n"
        "    vel = 100; };\n"
        "  sink { channel = 1; };\n"
        "};\n"
        "chain snare {\n"
        "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
        "    notes = \"D2\"; period = 1 beats; hold = 0.2 beats;\n"
        "    vel = 80; };\n"
        "  sink { channel = 2; };\n"
        "};\n";

    {
        /* Just under three bars, so the fourth -- which is the first one
           round again -- is not half in the count. */
        std::vector<Heard> h = playBody(plugins, synth, "sections", body,
                                        5.9);
        size_t kick[3] = { 0, 0, 0 }, snare[3] = { 0, 0, 0 };
        bool levelOk = true;

        for (size_t i = 0; i < h.size(); i++)
        {
            const int bar = (int)(h[i].at / 2.0 + 1e-9);

            if (bar < 0 || bar > 2)
            {
                fail("sections: a note landed outside the three bars");
                continue;
            }

            if (h[i].channel == 0)
            {
                kick[bar]++;

                if (h[i].vel != 100 ||
                    !near(h[i].level, bar == 2 ? 0.5 : 1.0))
                    levelOk = false;
            }
            else
            {
                snare[bar]++;

                if (h[i].vel != 80 || !near(h[i].level, 1.0))
                    levelOk = false;
            }
        }

        if (kick[0] != 4 || kick[1] != 0 || kick[2] != 4)
            fail("sections: the kick should play the first bar, sit out "
                 "the second and come back for the third; heard " +
                 std::to_string(kick[0]) + "/" + std::to_string(kick[1]) +
                 "/" + std::to_string(kick[2]));

        if (snare[0] != 0 || snare[1] != 4 || snare[2] != 0)
            fail("sections: the snare should be heard in the second bar "
                 "only; heard " + std::to_string(snare[0]) + "/" +
                 std::to_string(snare[1]) + "/" +
                 std::to_string(snare[2]));

        if (!levelOk)
            fail("sections: a level of 0.5 should halve voice gain and "
                 "leave velocities unchanged");
    }

    /* The list cycles: the fourth bar is the first section again. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "sections cycle",
                                        body, 7.9);
        size_t kick = 0, snare = 0;

        for (size_t i = 0; i < h.size(); i++)
            if (h[i].at >= 6.0)
                (h[i].channel == 0 ? kick : snare)++;

        if (kick != 4 || snare != 0)
            fail("sections: the list should cycle, so the fourth bar is "
                 "the first section again");
    }

    /* `section end;': the piece stops itself where it says it does, and
       the transport says so. */
    {
        const std::string ends =
            "tempo 120;\n"
            "meter 4;\n"
            "section only 1 bars { };\n"
            "section end;\n"
            "chain kick {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
            "    notes = \"C2\"; period = 1 beats; hold = 0.2 beats;\n"
            "    vel = 100; };\n"
            "  sink { channel = 1; };\n"
            "};\n";

        const std::string path = thUtil::tempFile("gencheck-secend-");

        if (path.empty())
            fail("could not write the section-end piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << ends;
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("the section-end piece did not load");
            else
            {
                /* render() stops at the end of the piece as well as at
                   the time asked for; four seconds is twice the
                   arrangement. */
                std::vector<Heard> h = notesOf(render(sched, 4.0, 0.02));

                if (h.size() != 4)
                    fail("section end: one bar of four should deliver four "
                         "notes and then nothing; delivered " +
                         std::to_string(h.size()));

                if (sched.running())
                    fail("section end: the transport did not stop itself");

                if (sched.now() > 2.05)
                    fail("section end: the transport stopped at " +
                         std::to_string(sched.now()) + " s, not at the "
                         "end of the last section");
            }

            remove(path.c_str());
        }
    }

    /* The things a section can get wrong, each by name and line. */
    expectReject(plugins, synth, "section-no-unit",
        "section a 8 { };\n"
        "chain c { stage s gen::eno_line { }; sink { channel = 1; }; };",
        "write a unit");

    expectReject(plugins, synth, "section-no-such-chain",
        "section a 8 bars { nope = 0; };\n"
        "chain c { stage s gen::eno_line { }; sink { channel = 1; }; };",
        "not a chain in this piece");

    expectReject(plugins, synth, "late-meter",
        "section a 8 bars { };\nmeter 3;\n"
        "chain c { stage s gen::eno_line { }; sink { channel = 1; }; };",
        "before the first section");

    expectReject(plugins, synth, "after-section-end",
        "section a 8 bars { };\nsection end;\nsection b 8 bars { };\n"
        "chain c { stage s gen::eno_line { }; sink { channel = 1; }; };",
        "nothing comes after");

    expectReject(plugins, synth, "negative-level",
        "section a 8 bars { c = -1; };\n"
        "chain c { stage s gen::eno_line { }; sink { channel = 1; }; };",
        "cannot be negative");

    expectReject(plugins, synth, "duplicate-section",
        "section a 8 bars { };\nsection a 4 bars { };\n"
        "chain c { stage s gen::eno_line { }; sink { channel = 1; }; };",
        "already declared");

    /* And the editor, which does not write an arrangement and must
       therefore not disturb one. */
    {
        const std::string path = thUtil::tempFile("gencheck-secedit-");

        if (path.empty())
        {
            fail("could not write the section-editing piece");
            return;
        }

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << "# the arrangement, with a comment in it\n" << body;
        }

        const std::string before = slurp(path);
        std::string why;

        /* The panel reads past the arrangement to the chains under it. */
        thcGenEdit::Doc doc;

        editOk(thcGenEdit::describe(path, doc, why), why,
               "describe over sections");

        if (doc.chains.size() != 2 || doc.chains[0].name != "kick")
            fail("describe did not read past the arrangement to the "
                 "chains under it");

        editOk(thcGenEdit::setTempo(path, 140, why), why,
               "setTempo over sections");
        editOk(thcGenEdit::setParam(path, "kick", 0, "vel", "110", why),
               why, "setParam over sections");

        if (sectionLines(slurp(path)) != sectionLines(before))
            fail("an edit elsewhere in the file rewrote the arrangement");

        /* A chain a section names cannot quietly go. */
        if (thcGenEdit::removeChain(path, "kick", why) != thcGenEdit::REFUSED)
            fail("removing a chain the arrangement names was allowed");

        /* A rename takes the arrangement with it. */
        editOk(thcGenEdit::renameChain(path, "kick", "boom", why), why,
               "renameChain under sections");

        const std::string after = slurp(path);

        if (after.find("boom = 0.5") == std::string::npos ||
            after.find("kick") != std::string::npos)
            fail("a renamed chain left the arrangement naming the old one");

        if (after.find("# the arrangement, with a comment in it") ==
            std::string::npos)
            fail("editing over an arrangement lost a comment");

        clearChannels(synth);
        drainSynth();

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(path, &sched))
        {
            for (size_t k = 0; k < loader.errors().size(); k++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[k].c_str());

            fail("the edited arrangement no longer loads");
        }
        else if (sched.sections().size() != 3 || sched.endsAfterSections())
            fail("the edited arrangement did not read back as three "
                 "cycling sections");

        remove(path.c_str());
    }
}

static void
checkChainStart (const std::map<std::string, thcPlugin *> &plugins,
                 thSynth *synth)
{
    const std::string body =
        "tempo 120;\n"
        "chain late {\n"
        "  stage src gen::euclid { steps = 1; fills = 1; notes = \"C4\";"
        " period = 1 s; hold = 0.1 s; };\n"
        "  start = 2 s;\n"
        "  sink { channel = 1; };\n"
        "};\n";
    const std::string path = thUtil::tempFile("gencheck-chain-start-");

    if (path.empty())
    {
        fail("could not write the delayed-chain piece");
        return;
    }

    {
        std::ofstream out(path.c_str(), std::ios::trunc);
        out << body;
    }

    thcScheduler sched(synth);
    thcGenLoader loader(plugins);

    if (!loader.load(path, &sched))
        fail("delayed chain did not load: " +
             (loader.errors().empty() ? std::string("unknown error") :
                                        loader.errors()[0]));
    else
    {
        const std::string first = render(sched, 3.1, 0.02);

        if (first.find("N 2 0 60 ") != 0 ||
            first.find("N 1 ") != std::string::npos)
            fail("delayed chain did not first sound at two seconds");

        sched.reset();

        if (render(sched, 3.1, 0.02) != first)
            fail("delayed chain changed after rewind");

        thcGenEdit::Doc doc;
        std::string why;

        if (thcGenEdit::describe(path, doc, why) != thcGenEdit::OK ||
            doc.chains.size() != 1 || doc.chains[0].startText != "2 s")
            fail("the editor lost the chain's authored start");

        if (thcGenEdit::setParam(path, "late", 0, "period", "1 s", why)
            != thcGenEdit::OK || slurp(path) != body)
            fail("the writer changed the delayed chain while editing a stage");

        /* And the writer the panel's entry calls: a start moved, then
           taken out, then put back on a chain that has none -- the last
           of the three is the insert, which goes above the stages. */
        auto startOf = [&](void)
        {
            thcGenEdit::Doc d;
            std::string w;

            return thcGenEdit::describe(path, d, w) == thcGenEdit::OK &&
                   d.chains.size() == 1 ? d.chains[0].startText
                                        : std::string("<unreadable>");
        };

        if (thcGenEdit::setChainStart(path, "late", "4 beats", why)
                != thcGenEdit::OK || startOf() != "4 beats")
            fail("the writer would not move the chain's start");

        if (thcGenEdit::setChainStart(path, "late", "", why)
                != thcGenEdit::OK || !startOf().empty())
            fail("the writer would not take the chain's start out");

        if (thcGenEdit::setChainStart(path, "late", "1 bars", why)
                != thcGenEdit::OK || startOf() != "1 bars")
            fail("the writer would not give a chain a start it had none");

        thcScheduler after(synth);
        thcGenLoader reload(plugins);

        if (!reload.load(path, &after))
            fail("the piece the start writer left did not load: " +
                 (reload.errors().empty() ? std::string("unknown error")
                                          : reload.errors()[0]));

        if (thcGenEdit::setChainStart(path, "late", "2", why)
                != thcGenEdit::REFUSED ||
            thcGenEdit::setChainStart(path, "late", "2 fortnights", why)
                != thcGenEdit::REFUSED ||
            thcGenEdit::setChainStart(path, "nosuch", "2 s", why)
                != thcGenEdit::NOT_FOUND)
            fail("the start writer accepted what the loader would refuse");
    }

    std::filesystem::remove(path);

    const std::string clocked =
        "tempo 120;\nchain c { start = 4 beats;"
        " stage s gen::euclid { steps = 1; fills = 1; notes = \"C4\";"
        " period = 1 s; hold = 0.1 s; };"
        " sink { channel = 1; }; };\n";
    const std::string beatTape = renderBody(plugins, synth, "chain-beat-start",
                                             clocked, 2.1);

    if (beatTape.find("N 2 0 60 ") != 0)
        fail("a chain start in beats did not follow the tempo");

    {
        const std::string beatPath = thUtil::tempFile("gencheck-beat-start-");

        if (!beatPath.empty())
        {
            {
                std::ofstream out(beatPath.c_str(), std::ios::trunc);
                out << clocked;
            }

            thcScheduler clock(synth);
            thcGenLoader beatLoader(plugins);

            if (!beatLoader.load(beatPath, &clock))
                fail("could not load the clocked chain");
            else
            {
                std::vector<double> heard;
                sigc::connection conn = clock.sigDelivered.connect(
                    [&heard](const thcEvent &ev) {
                        if (ev.type == THC_EV_NOTE)
                            heard.push_back(ev.at);
                    });

                clock.start();
                clock.stepTransportTo(1.0);
                clock.setTempo(60);
                clock.stepTransportTo(2.9);

                if (!heard.empty())
                    fail("beat-valued chain started before the new tempo's beat");

                clock.stepTransportTo(3.0);

                if (heard.empty() || !near(heard[0], 3.0))
                    fail("beat-valued chain did not move with a tempo change");

                conn.disconnect();
            }

            std::filesystem::remove(beatPath);
        }
        else
            fail("could not write the clocked-chain piece");
    }

    expectReject(plugins, synth, "chain-start-unit",
        "chain c { start = 2; stage s gen::eno_line { };"
        " sink { channel = 1; }; };", "start needs a unit");
    expectReject(plugins, synth, "chain-start-twice",
        "chain c { start = 1 s; start = 2 s;"
        " stage s gen::eno_line { }; sink { channel = 1; }; };",
        "sets start twice");
    /* A chain written on one line. An insert anchored on the first
       newline after the `chain' keyword lands past the chain's own
       closing brace here, which is a file the next load refuses. */
    {
        const std::string flat =
            "chain one { stage s gen::euclid { steps = 1; fills = 1;"
            " notes = \"C4\"; period = 1 s; hold = 0.1 s; };"
            " sink { channel = 1; }; };\n";
        const std::string path = thUtil::tempFile("gencheck-flat-chain-");
        std::string why;

        if (path.empty())
            fail("could not write the one-line chain");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);
                out << flat;
            }

            if (thcGenEdit::setChainStart(path, "one", "1 s", why)
                    != thcGenEdit::OK ||
                thcGenEdit::setChainInput(path, "one", true, why)
                    != thcGenEdit::OK)
                fail("the editor would not write into a one-line chain");

            thcGenEdit::Doc doc;

            if (thcGenEdit::describe(path, doc, why) != thcGenEdit::OK ||
                doc.chains.size() != 1 ||
                doc.chains[0].startText != "1 s" ||
                !doc.chains[0].inputMidi)
                fail("a one-line chain lost what the editor wrote into it: "
                     + slurp(path));

            clearChannels(synth);
            drainSynth();

            thcScheduler flatSched(synth);
            thcGenLoader flatLoader(plugins);

            if (!flatLoader.load(path, &flatSched))
                fail("the one-line chain the editor wrote does not load: " +
                     (flatLoader.errors().empty()
                          ? std::string("unknown error")
                          : flatLoader.errors()[0]));

            std::filesystem::remove(path);
        }
    }

    expectReject(plugins, synth, "chain-start-after-sink",
        "chain c { stage s gen::eno_line { }; sink { channel = 1; };"
        " start = 1 s; };", "start after sink");
    expectReject(plugins, synth, "chain-start-negative",
        "chain c { start = -1 s; stage s gen::eno_line { };"
        " sink { channel = 1; }; };", "start wants a nonnegative time");

    /* A bar is meter beats, folded as the start is read, so a meter
       under one could not mean what it says -- the same rule a section
       in bars is held to, and the one this adds a second caller to. */
    expectReject(plugins, synth, "chain-start-bars-then-meter",
        "chain c { start = 1 bars; stage s gen::eno_line { };"
        " sink { channel = 1; }; };\nmeter 3;",
        "meter must come before the first section or chain start in bars");

    /* A start holds generators back, and a chain fed by live MIDI has
       none to hold. */
    expectReject(plugins, synth, "chain-start-no-generator",
        "chain c { start = 1 s; input midi;"
        " stage x xform::echo { }; sink { channel = 1; }; };",
        "has no generator stage");
}


/* ---- voice leading (PIECES_PLAN.md 3a) ---------------------------------
 *
 * A chord is which notes are in it; a voicing is which octave each of
 * them is sung in. xform::harmonize only ever had the second one
 * answer to the first -- stack the degrees on the root, wherever the
 * root happens to be -- so a progression moved in parallel blocks and
 * the ear heard three chords rather than three voices. `lead' keeps the
 * notes and chooses the octaves, and what is asked here is exactly
 * that split:
 *
 * 1. The led chord has the same notes as the stacked one. A stage that
 *    quietly spelled something else when it was turned on would be a
 *    second harmonizer wearing this one's name, and no amount of
 *    smooth voice leading would make up for it.
 * 2. It moves the voices less. Measured on the tape and compared
 *    against the stacking, because "sounds smoother" is not a gate.
 * 3. No voice moves more than a fourth, and one of them holds its
 *    pitch across a chord change -- the common tone, which is the
 *    thing a listener actually hears.
 * 4. `lead = 0' is the stack it always was.
 * 5. Repeated roots do not drift: the same chord twice running is the
 *    same voicing twice running.
 * 6. `span' is a fence around the root. A root an octave down drags
 *    its chord after it, and a span wide enough to allow it leaves the
 *    voices where they were.
 * 7. No voice lands on the root's own pitch, which is a unison and a
 *    voice thrown away.
 */

/* The chords of a tape: the notes that arrive together, low to high. */
static std::vector<std::vector<int> >
chordsOf (const std::vector<Heard> &h)
{
    std::vector<std::vector<int> > out;

    for (size_t i = 0; i < h.size(); i++)
    {
        if (out.empty() || h[i].at - h[i - 1].at > 1e-6)
            out.push_back(std::vector<int>());

        out.back().push_back(h[i].note);
    }

    for (size_t i = 0; i < out.size(); i++)
        std::sort(out[i].begin(), out[i].end());

    return out;
}

/* How far the voices travel from one chord to the next, added up, and
 * the furthest any single one of them goes.
 *
 * The pairing is by register -- the lowest voice of one chord answers
 * the lowest of the next -- because that is all "a voice" can mean once
 * the chords are only pitches on a tape, and because a voicing that
 * crossed its voices to look still would be a worse voicing anyway. */
static long
travel (const std::vector<std::vector<int> > &c, int *worst)
{
    long sum = 0;

    if (worst)
        *worst = 0;

    for (size_t i = 1; i < c.size(); i++)
    {
        if (c[i].size() != c[i - 1].size())
            continue;

        for (size_t v = 0; v < c[i].size(); v++)
        {
            const int d = abs(c[i][v] - c[i - 1][v]);

            sum += d;

            if (worst && d > *worst)
                *worst = d;
        }
    }

    return sum;
}

static void
checkVoiceLeading (const std::map<std::string, thcPlugin *> &plugins,
                   thSynth *synth)
{
    {
        const char *need[] = { "harmonize", "euclid", NULL };

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                return;
            }
    }

    /* C major from C3, a chord a second, `taper = 1' and no spread so
       the tape is four plain triads. The roots are C F G C: the
       progression every first harmony lesson opens with, and the one
       everybody already knows the answer to -- the C stays put and
       nothing else goes further than a step. */
    const char *shape =
        "chain c {\n"
        "  stage src gen::euclid { steps = 4; fills = 4;"
        "    notes = \"%s\"; period = 1 s; hold = 0.9 s; };\n"
        "  stage h xform::harmonize { scale = \"C3 D3 E3 F3 G3 A3 B3\";"
        "    voices = %d; step = %d; spread = 0 s; taper = 1;"
        "    below = %d; lead = %d; span = %d; };\n"
        "  sink { channel = 1; };\n"
        "};\n";

    char body[1024];

    /* ---- 1, 4. the same notes, and the stack when nobody asks ---- */

    for (int below = 0; below < 2; below++)
    {
        std::vector<std::vector<int> > stacked, led;

        snprintf(body, sizeof(body), shape, "C3 F3 G3 C3", 3, 2, below,
                 0, 12);
        stacked = chordsOf(playBody(plugins, synth, "harmonize-stacked",
                                    body, 3.5));

        snprintf(body, sizeof(body), shape, "C3 F3 G3 C3", 3, 2, below,
                 1, 12);
        led = chordsOf(playBody(plugins, synth, "harmonize-led", body,
                                3.5));

        if (stacked.size() != 4 || led.size() != 4)
        {
            fail("voice leading: expected four chords either way, got " +
                 std::to_string(stacked.size()) + " stacked and " +
                 std::to_string(led.size()) + " led");
            return;
        }

        for (size_t i = 0; i < led.size(); i++)
        {
            std::multiset<int> a, b;

            for (size_t v = 0; v < led[i].size(); v++)
                a.insert(led[i][v] % 12);

            for (size_t v = 0; v < stacked[i].size(); v++)
                b.insert(stacked[i][v] % 12);

            if (a != b)
                fail(below ? "voice leading: a led chord below the melody "
                             "is not the chord the stack spelled"
                           : "voice leading: a led chord is not the chord "
                             "the stack spelled");
        }

        if (below)
            continue;

        /* The stack, unchanged, note for note: F with two degrees of C
           major over it is 53 57 60 and has been since the plugin was
           written. */
        if (stacked[1].size() != 3 || stacked[1][0] != 53 ||
            stacked[1][1] != 57 || stacked[1][2] != 60)
            fail("voice leading: `lead = 0' is no longer the stack it "
                 "always was");

        /* ---- 2, 3. less movement, and a common tone ---- */

        int worstLed = 0, worstStacked = 0;
        const long moveLed = travel(led, &worstLed);
        const long moveStacked = travel(stacked, &worstStacked);

        if (moveLed >= moveStacked)
            fail("voice leading: the led voicings move the voices " +
                 std::to_string(moveLed) + " semitones against the "
                 "stack's " + std::to_string(moveStacked) +
                 "; leading them is supposed to be the point");

        if (worstLed > 5)
            fail("voice leading: a led voice moved " +
                 std::to_string(worstLed) + " semitones, which is further "
                 "than the fourth C F G C asks of anybody");

        bool common = false;

        for (size_t i = 1; i < led.size() && !common; i++)
            for (size_t v = 0; v < led[i].size(); v++)
                if (std::find(led[i - 1].begin(), led[i - 1].end(),
                              led[i][v]) != led[i - 1].end())
                    common = true;

        if (!common)
            fail("voice leading: no voice held its pitch across a chord "
                 "change, and C F G C has a common tone in it");
    }

    /* ---- 5. a chord that does not change does not move ---- */

    /* The drift gate. Every chord is placed relative to the one before
       it, so an error of one octave per chord is a shape this could
       have and nothing else here would catch: four bars of one chord
       must be four bars of one voicing. */
    {
        snprintf(body, sizeof(body), shape, "C3 C3 C3 C3", 3, 2, 0, 1, 12);

        const std::vector<std::vector<int> > same =
            chordsOf(playBody(plugins, synth, "harmonize-still", body,
                              3.5));

        if (same.size() != 4)
            fail("voice leading: expected four chords, got " +
                 std::to_string(same.size()));
        else
            for (size_t i = 1; i < same.size(); i++)
                if (same[i] != same[0])
                    fail("voice leading: one chord, played four times, "
                         "was voiced differently the second time");
    }

    /* ---- 6. `span' is a fence around the root ---- */

    /* Two octaves of roots. With a fence of an octave the voices have
       to follow the root down; with one of four they may stay where
       they are, and staying is what moves them least. Both answers are
       right and the param is the difference between them, which is the
       only way to show it is read at all. */
    {
        for (int span = 12; span <= 48; span += 36)
        {
            snprintf(body, sizeof(body), shape, "C3 C2 C3 C2", 3, 2, 0, 1,
                     span);

            const std::vector<std::vector<int> > c =
                chordsOf(playBody(plugins, synth, "harmonize-span", body,
                                  3.5));

            if (c.size() != 4 || c[1].size() != 3)
            {
                fail("voice leading: the span piece did not play its "
                     "chords");
                continue;
            }

            /* The root is 36 in that chord either way. What is asked
               is where the two voices over the C3 before it went. */
            const bool stayed =
                std::find(c[1].begin(), c[1].end(), 52) != c[1].end() &&
                std::find(c[1].begin(), c[1].end(), 55) != c[1].end();

            if (span == 12)
            {
                for (size_t v = 0; v < c[1].size(); v++)
                    if (abs(c[1][v] - 36) > 12)
                        fail("voice leading: a voice sat " +
                             std::to_string(abs(c[1][v] - 36)) +
                             " semitones from a root that fenced them "
                             "to twelve");
            }
            else if (!stayed)
                fail("voice leading: a fence four octaves wide still "
                     "dragged the voices down after the root");
        }
    }

    /* ---- 7. never a unison with the root ---- */

    /* `step = 7' in a seven-note scale puts the second voice an octave
       over the first, so the chord's other note is the root's own pitch
       class and the register that moves it least is the root's own.
       Least movement is not the only rule: a voice on the root is a
       voice nobody can hear. */
    {
        snprintf(body, sizeof(body), shape, "C3 C4 C3 C4", 2, 7, 0, 1, 12);

        const std::vector<std::vector<int> > c =
            chordsOf(playBody(plugins, synth, "harmonize-unison", body,
                              3.5));

        if (c.size() != 4)
            fail("voice leading: the unison piece did not play its "
                 "chords");
        else
            for (size_t i = 0; i < c.size(); i++)
                if (c[i].size() != 2 || c[i][0] == c[i][1])
                    fail("voice leading: a led voice landed on the root's "
                         "own pitch");
    }
}

/* ---- the graph on the mix (PIECES_PLAN.md 4d) ---------------------------
 *
 * A top-level `effect' statement: the same clause an instrument carries,
 * aimed at the sum of every channel. What the engine does with it is
 * fxcheck's -- that it is fed the mix, that it runs when nothing is playing,
 * that it comes off again. What is asked here is the language's half:
 *
 * 1. The statement is read and the graph is on the mix, with the values the
 *    file wrote actually in its args.
 * 2. A piece that declares none takes off whatever the last one left. A
 *    session opens pieces one after another; a reverb that outlived the
 *    piece that asked for it would be somebody else's.
 * 3. The errors are errors: a chanarg the graph does not declare, two
 *    statements, a file that is not there.
 * 4. The writer leaves the statement alone, byte for byte, when an edit
 *    aimed somewhere else rewrites the file.
 */
static void
checkMasterEffect (const std::map<std::string, thcPlugin *> &plugins,
                   thSynth *synth)
{
    const char *shape =
        "effect \"fx/limiter.dsp\" {\n"
        "    drive   = 2;\n"
        "    ceiling = 0.75;\n"
        "};\n"
        "instrument plink {\n"
        "    dsp \"pluck.dsp\";\n"
        "    amp = 20;\n"
        "};\n"
        "chain c {\n"
        "  stage src gen::euclid { steps = 2; fills = 2; notes = \"A3 C4\";"
        "    period = 1 s; hold = 0.5 s; };\n"
        "  sink { instrument = plink; };\n"
        "};\n";

    /* ---- 1. the statement puts a graph on the mix ---- */

    {
        clearChannels(synth);
        drainSynth();

        const std::string path = thUtil::tempFile("gencheck-master-");

        if (path.empty())
        {
            fail("could not write the master-effect piece");
            return;
        }

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << shape;
        }

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(path, &sched))
        {
            for (size_t k = 0; k < loader.errors().size(); k++)
                fprintf(stderr, "gencheck: %s\n", loader.errors()[k].c_str());

            fail("the master-effect piece did not load");
        }
        else if (synth->getMasterEffect() == NULL)
            fail("a piece with an `effect' statement left the mix with no "
                 "effect on it");
        else
        {
            thArg *drive = synth->getMasterArg("drive");
            thArg *ceiling = synth->getMasterArg("ceiling");

            if (drive == NULL || ceiling == NULL)
                fail("the master effect's chanargs are not reachable by "
                     "name");
            else if (!near((*drive)[0], 2) || !near((*ceiling)[0], 0.75))
                fail("the master effect's values are not the ones the piece "
                     "wrote (drive " + std::to_string((*drive)[0]) +
                     ", ceiling " + std::to_string((*ceiling)[0]) + ")");

            /* And it plays. A master effect that silenced the piece would
               pass every check above. */
            if (render(sched, 4.0, 0.02).find("N ") == std::string::npos)
                fail("the master-effect piece delivered nothing");
        }

        /* ---- 2. and the next piece takes it off ---- */

        std::string plain = shape;
        const size_t at = plain.find("effect \"fx/limiter.dsp\"");

        plain.erase(at, plain.find("};\n", at) + 3 - at);

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << plain;
        }

        clearChannels(synth);
        drainSynth();

        {
            thcScheduler second(synth);
            thcGenLoader again(plugins);

            if (!again.load(path, &second))
                fail("the piece without a master effect did not load");
            else
            {
                /* The removal is a command, like every other change to what
                   the audio thread is running. */
                drainSynth();

                if (synth->getMasterEffect() != NULL)
                    fail("a piece that declares no master effect left the "
                         "last piece's on the mix");
            }
        }

        remove(path.c_str());
        clearChannels(synth);
        drainSynth();
    }

    /* ---- 3. the three ways to get it wrong ---- */

    expectReject(plugins, synth, "master-effect-no-such-arg",
        "effect \"fx/limiter.dsp\" { nonesuch = 1; };\n"
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; }; };",
        "nonesuch");

    expectReject(plugins, synth, "master-effect-twice",
        "effect \"fx/limiter.dsp\";\n"
        "effect \"fx/hall.dsp\";\n"
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; }; };",
        "two master effects");

    expectReject(plugins, synth, "master-effect-missing",
        "effect \"fx/nosuchthing.dsp\";\n"
        "chain c { stage s gen::eno_line { };"
        " sink { channel = 1; }; };",
        "nosuchthing");

    clearChannels(synth);
    drainSynth();

    /* ---- 4. the writer leaves it alone ---- */

    /* The statement is nobody's to edit -- what runs on the mix is written
       by hand -- so what the writer owes it is that an edit aimed at a stage
       leaves it exactly as it was. The same promise the arrangement gets,
       and the same gate. */
    {
        const std::string path = thUtil::tempFile("gencheck-master-edit-");

        if (path.empty())
        {
            fail("could not write the master-effect piece");
            return;
        }

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << "# a piece with something on its mix\n"
                << shape;
        }

        const std::string before = slurp(path);
        std::string why;

        editOk(thcGenEdit::setParam(path, "c", 0, "period", "2 s", why), why,
               "editing a stage in a piece with a master effect");

        const std::string after = slurp(path);

        if (after.find("effect \"fx/limiter.dsp\" {\n"
                       "    drive   = 2;\n"
                       "    ceiling = 0.75;\n"
                       "};") == std::string::npos)
            fail("an edit elsewhere rewrote the master effect statement");

        if (after.find("# a piece with something on its mix") ==
            std::string::npos)
            fail("an edit in a piece with a master effect lost a comment");

        if (after.find("period = 2 s") == std::string::npos)
            fail("the edit itself did not happen");

        if (after == before)
            fail("the edit wrote nothing at all");

        /* And it still loads, which is the other half of "byte for byte". */
        clearChannels(synth);
        drainSynth();

        {
            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("the edited piece with a master effect no longer loads");
        }

        remove(path.c_str());
        clearChannels(synth);
        drainSynth();
    }
}

/* ---- a scale pickup before a written note ------------------------------ */

static void
checkRun (const std::map<std::string, thcPlugin *> &plugins,
          thSynth *synth)
{
    if (plugins.find("run") == plugins.end())
    {
        fail("module 'run' is missing; build the plugins first");
        return;
    }

    auto warningsFor = [&](const std::string &source,
                           const std::string &prelude = "")
    {
        std::vector<std::string> warnings;
        const std::string path = thUtil::tempFile("gencheck-ahead-warning-");

        if (path.empty())
        {
            fail("could not write the lookahead warning piece");
            return warnings;
        }

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << prelude
                << "chain c { stage src " << source << ";\n"
                   "  stage pick xform::run { steps = 2; time = 0.5 s;"
                   " prob = 1; };\n"
                   "  sink { channel = 1; }; };\n";
        }

        clearChannels(synth);
        drainSynth();

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(path, &sched))
            fail("the lookahead warning piece did not load");
        else
            warnings = loader.warnings();

        std::filesystem::remove(path);
        return warnings;
    };

    {
        const std::vector<std::string> plain = warningsFor(
            "gen::euclid { steps = 4; fills = 1; notes = \"C4\";"
            " period = 1 s; hold = 0.1 s; }");
        const std::vector<std::string> ahead = warningsFor(
            "gen::euclid { steps = 4; fills = 1; notes = \"C4\";"
            " period = 1 s; hold = 0.1 s; ahead = 1; }");
        const std::vector<std::string> phrase = warningsFor(
            "gen::lsystem { axiom = \"rF\"; depth = 0;"
            " notes = \"C4\"; step = 1 s; hold = 0.1 s; }");

        if (plain.size() != 1 ||
            plain[0].find("src") == std::string::npos ||
            plain[0].find("euclid") == std::string::npos ||
            plain[0].find("pick") == std::string::npos ||
            plain[0].find("run") == std::string::npos ||
            !ahead.empty() || !phrase.empty())
            fail("run: warn for a stepwise source, not a source that "
                 "emits ahead");

        if (plain[0].find("set ahead = 1") == std::string::npos)
            fail("run: the warning for a source with an ahead param "
                 "should say to set it");
    }

    /* A knob on `ahead' is not a lookahead: the warning still goes out,
       and telling this file to set a param it already sets would read
       as a bug in the loader rather than as the answer. */
    {
        const std::vector<std::string> bound = warningsFor(
            "gen::euclid { steps = 4; fills = 1; notes = \"C4\";"
            " period = 1 s; hold = 0.1 s; ahead = @look; }",
            "@look = 1;\n");

        if (bound.size() != 1 ||
            bound[0].find("bound") == std::string::npos ||
            bound[0].find("set ahead = 1") != std::string::npos)
            fail("run: a knob-bound ahead should warn, and should not be "
                 "told to set ahead = 1");
    }

    {
        const std::string body =
            "chain c { stage src gen::euclid { steps = 4; fills = 3;"
            " rotate = 1; notes = \"C4 D4 E4\"; fill = \"F4 G4\";"
            " every = 2; period = 0.25 s; hold = 0.1 s; vel = 90;"
            " ahead = %s; }; sink { channel = 1; }; };\n";
        std::string plain = body, ahead = body;

        plain.replace(plain.find("%s"), 2, "0");
        ahead.replace(ahead.find("%s"), 2, "1");

        const std::string plainTape =
            renderBody(plugins, synth, "stepwise ring", plain, 3.1);
        const std::string aheadTape =
            renderBody(plugins, synth, "ahead ring", ahead, 3.1);

        if (plainTape.empty() || plainTape != aheadTape)
            fail("euclid: ahead and stepwise cycles differ in their notes");
    }

    auto firstPickupArrival = [&](int ahead)
    {
        const std::string path = thUtil::tempFile("gencheck-ahead-delivery-");

        if (path.empty())
        {
            fail("could not write the pickup delivery piece");
            return -1.0;
        }

        {
            std::ofstream out(path.c_str(), std::ios::trunc);

            out << "scale cmaj \"C4 D4 E4 F4 G4 A4 B4\";\n"
                   "chain c { stage src gen::euclid { steps = 4;"
                   " fills = 1; rotate = 2; notes = \"C4\";"
                   " period = 1 s; hold = 0.5 s; ahead = " << ahead
                << "; };\n"
                   "stage pick xform::run { scale = cmaj; steps = 2;"
                   " time = 1 s; prob = 1; };\n"
                   "sink { channel = 1; }; };\n";
        }

        clearChannels(synth);
        drainSynth();

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);
        double arrival = -1;

        if (!loader.load(path, &sched))
            fail("the pickup delivery piece did not load");
        else
        {
            sigc::connection conn = sched.sigDelivered.connect(
                [&](const thcEvent &ev)
                {
                    if (ev.type == THC_EV_NOTE && ev.at < 2 && arrival < 0)
                        arrival = sched.now();
                });

            render(sched, 3.1, 0.02);
            conn.disconnect();
        }

        std::filesystem::remove(path);
        return arrival;
    };

    {
        const double ahead = firstPickupArrival(1);
        const double plain = firstPickupArrival(0);

        if (fabs(ahead - 1.0) > 0.03 || fabs(plain - 2.0) > 0.03)
            fail("run: a cycle emitted ahead must deliver its pickup "
                 "before the target, not at the target's wake");
    }

    /* The grammar emits its whole phrase at transport zero. Its two
       rests place C4 at 2 s, early enough for the transformer to send
       a one-second pickup into the scheduler before the target sounds. */
    const std::string line =
        "seed 3;\n"
        "scale cmaj \"C4 D4 E4 F4 G4 A4 B4\";\n"
        "chain c {\n"
        "  stage src gen::lsystem { axiom = \"rrF\"; depth = 0;\n"
        "    notes = \"C4\"; step = 1 s; hold = 0.5 s; vel = 100; };\n"
        "  stage r xform::run { scale = cmaj; time = 1 s; %s };\n"
        "  sink { channel = 1; };\n"
        "};\n";

    auto played = [&](const char *what, const std::string &params)
    {
        std::string body = line;

        body.replace(body.find("%s"), 2, params);

        return playBody(plugins, synth, what, body, 3.1);
    };

    {
        const std::vector<Heard> h =
            played("ascending run", "steps = 7; prob = 1; vel = 70;");
        const int pitch[] = { 48, 50, 52, 53, 55, 57, 59, 60 };
        bool good = h.size() == 8;

        for (size_t i = 0; i < h.size() && good; i++)
        {
            const double at = i < 7 ? 1 + (double)i / 7 : 2;
            const double dur = i < 7 ? 1.0 / 7 : 0.5;
            const int vel = i < 7 ? 70 : 100;

            if (h[i].note != pitch[i] || !near(h[i].at, at) ||
                !near(h[i].dur, dur) || h[i].vel != vel)
                good = false;
        }

        if (!good)
            fail("run: seven scale notes must precede the unchanged "
                 "target at equal intervals");
    }

    {
        const std::vector<Heard> h =
            played("descending run", "steps = -3; prob = 1;");
        const int pitch[] = { 65, 64, 62, 60 };
        bool good = h.size() == 4;

        for (size_t i = 0; i < h.size() && good; i++)
            if (h[i].note != pitch[i] ||
                !near(h[i].at, i < 3 ? 1 + (double)i / 3 : 2) ||
                h[i].vel != 100)
                good = false;

        if (!good)
            fail("run: negative steps descend from above into the target");
    }

    {
        const std::vector<Heard> through =
            played("run bypass", "steps = 7; prob = 0;");
        const std::vector<Heard> plain = playBody(plugins, synth,
            "run plain", "seed 3;\n"
            "chain c { stage src gen::lsystem { axiom = \"rrF\";"
            " depth = 0; notes = \"C4\"; step = 1 s; hold = 0.5 s;"
            " vel = 100; }; sink { channel = 1; }; };\n", 3.1);

        if (through.size() != plain.size() || through.size() != 1 ||
            through[0].note != plain[0].note ||
            through[0].vel != plain[0].vel ||
            !near(through[0].at, plain[0].at) ||
            !near(through[0].dur, plain[0].dur))
            fail("run: prob = 0 must pass the phrase unchanged");
    }

    {
        std::string body = line;

        body.replace(body.find("rrF"), 3, "rrFFFF");
        body.replace(body.find("%s"), 2,
                     "steps = 3; prob = 0.5; vel = 70;");

        const std::string first =
            renderBody(plugins, synth, "run replay", body, 6.0);
        const std::string again =
            renderBody(plugins, synth, "run replay again", body, 6.0);

        if (first.empty() || first != again)
            fail("run: a seeded phrase must replay identically");
    }

    /* No pickup can precede the first instant of a piece. */
    {
        std::string early = line;

        early.replace(early.find("rrF"), 3, "F");
        early.replace(early.find("%s"), 2, "steps = 7; prob = 1;");

        const std::vector<Heard> h =
            playBody(plugins, synth, "early run", early, 0.9);

        if (h.size() != 1 || h[0].note != 60 || !near(h[0].at, 0))
            fail("run: a target at transport zero passes through (heard " +
                 std::to_string(h.size()) +
                 (h.empty() ? "" : ", first " + std::to_string(h[0].note) +
                  " at " + std::to_string(h[0].at)) + ")");
    }

    /* A live knob can carry a non-finite value into a numeric param.
       That must leave the scheduled target alone. */
    {
        const std::string path = thUtil::tempFile("gencheck-run-nan-");

        if (path.empty())
            fail("run: could not write the non-finite probability piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "seed 3;\n"
                       "@chance = 1;\n"
                       "scale cmaj \"C4 D4 E4 F4 G4 A4 B4\";\n"
                       "chain c {\n"
                       "  stage src gen::lsystem { axiom = \"rrF\";"
                       " depth = 0; notes = \"C4\"; step = 1 s;"
                       " hold = 0.5 s; vel = 100; };\n"
                       "  stage r xform::run { scale = cmaj; steps = 7;"
                       " time = 1 s; prob = @chance; };\n"
                       "  sink { channel = 1; };\n"
                       "};\n";
            }

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            clearChannels(synth);
            drainSynth();

            if (!loader.load(path, &sched))
                fail("run: the non-finite probability piece did not load");
            else if (sched.knob("chance") == NULL)
                fail("run: the probability knob is missing");
            else
            {
                sched.knob("chance")->setValue(
                    std::numeric_limits<float>::quiet_NaN());

                const std::vector<Heard> h =
                    notesOf(render(sched, 3.1, 0.02));

                if (h.size() != 1 || h[0].note != 60 ||
                    !near(h[0].at, 2))
                    fail("run: non-finite probability must pass the "
                         "target through");
            }

            std::filesystem::remove(path);
        }
    }

    /* A live key has no known duration. Even after transport has run
       long enough for a pickup, its press and release must stay paired. */
    {
        const std::string path = thUtil::tempFile("gencheck-run-held-");

        if (path.empty())
            fail("run: could not write the held-note piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "seed 3;\n"
                       "chain c { input midi;"
                       " stage r xform::run { steps = 7; time = 1 s;"
                       " prob = 1; }; sink { channel = 1; }; };\n";
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("run: the held-note piece did not load");
            else
            {
                int ons = 0, offs = 0;
                sigc::connection conn = sched.sigDelivered.connect(
                    [&ons, &offs](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NOTE)
                            ons++;
                        else if (ev.type == THC_EV_NOTEOFF)
                            offs++;
                    });

                sched.start();
                sched.stepTransport(2.0);

                thcEvent ev = {};

                ev.type = THC_EV_NOTE;
        ev.u.note.level = 1;
                ev.at = sched.now();
                ev.channel = 0;
                ev.u.note.note = 60;
                ev.u.note.velocity = 100;
                ev.u.note.duration = 0;
                sched.injectMidiEvent(ev);

                sched.stepTransport(0.1);

                ev.type = THC_EV_NOTEOFF;
                ev.at = sched.now();
                sched.injectMidiEvent(ev);

                sched.stepTransport(0.1);
                sched.stop();
                conn.disconnect();
                drainSynth();

                if (ons != 1 || offs != 1)
                    fail("run: a held note and its release must pass "
                         "through unchanged; heard " +
                         std::to_string(ons) + " on and " +
                         std::to_string(offs) + " off");
            }

            std::filesystem::remove(path);
        }
    }
}

/* ---- variation (PIECES_PLAN.md 2) --------------------------------------
 *
 * The three stages that stop a written line repeating itself exactly:
 * xform::vary, which does one of six things to each note; xform::accent,
 * which weights a note by where it falls; and gen::euclid's fill pool,
 * which answers three bars of a riff with a fourth.
 */
static void
checkVariation (const std::map<std::string, thcPlugin *> &plugins,
                thSynth *synth)
{
    {
        const char *need[] = { "vary", "accent", "euclid", NULL };

        for (int i = 0; need[i] != NULL; i++)
            if (plugins.find(need[i]) == plugins.end())
            {
                fail(std::string("module '") + need[i] +
                     "' is missing; build the plugins first");
                return;
            }
    }

    /* Four sixteenths a second, one pitch, one velocity: the line every
       test below varies. `%s' is what the vary stage is asked for. */
    const std::string line =
        "seed 3;\n"
        "scale cmaj \"C4 D4 E4 F4 G4 A4 B4\";\n"
        "chain c {\n"
        "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
        "    notes = \"C4\"; period = 0.5 s; hold = 0.25 s; vel = 90; };\n"
        "  stage v xform::vary { scale = cmaj; grid = 0.5 s; %s };\n"
        "  sink { channel = 1; };\n"
        "};\n";

    auto varied = [&](const char *what, const std::string &params,
                      double seconds)
    {
        std::string body = line;

        body.replace(body.find("%s"), 2, params);

        return playBody(plugins, synth, what, body, seconds);
    };

    /* Nothing asked for, nothing done: the phrase comes out as it went
       in. Held against the same piece with no vary stage at all, which
       is the only statement of "unchanged" worth making. */
    {
        std::vector<Heard> through = varied("vary passthrough", "", 2.1);
        std::vector<Heard> plain = playBody(plugins, synth, "vary plain",
            "seed 3;\n"
            "chain c {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
            "    notes = \"C4\"; period = 0.5 s; hold = 0.25 s;"
            "    vel = 90; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 2.1);

        if (through.size() != plain.size() || through.empty())
            fail("vary: a stage at its defaults changed how many notes "
                 "there are (" + std::to_string(through.size()) + " against "
                 + std::to_string(plain.size()) + ")");
        else
            for (size_t i = 0; i < through.size(); i++)
                if (through[i].note != plain[i].note ||
                    through[i].vel != plain[i].vel ||
                    !near(through[i].at, plain[i].at) ||
                    !near(through[i].dur, plain[i].dur))
                {
                    fail("vary: a stage at its defaults is not a "
                         "pass-through");
                    break;
                }
    }

    /* Every probability in turn, at 1, so what each one does is a fact
       about every note rather than about some of them. */
    {
        if (!varied("vary rest", "rest = 1;", 2.1).empty())
            fail("vary: rest = 1 should leave nothing to hear");
    }

    {
        std::vector<Heard> h = varied("vary leap", "leap = 1;", 2.1);

        if (h.empty())
            fail("vary: leap = 1 dropped the line");

        for (size_t i = 0; i < h.size(); i++)
            if (h[i].note != 72 && h[i].note != 48)
            {
                fail("vary: leap = 1 should move every note an octave, "
                     "not to " + std::to_string(h[i].note));
                break;
            }
    }

    {
        std::vector<Heard> h = varied("vary push", "push = 1;", 2.1);
        bool ok = !h.empty();

        for (size_t i = 0; i < h.size(); i++)
        {
            /* Every note is a grid step from where it was written, on
               one side or the other. */
            const double from = h[i].at;
            bool onGrid = false;

            for (double k = 0; k <= 5; k++)
                if (near(from, k * 0.5 + 0.5) || near(from, k * 0.5 - 0.5))
                    onGrid = true;

            if (!onGrid)
                ok = false;
        }

        if (!ok)
            fail("vary: push = 1 should move every note one grid step");
    }

    {
        std::vector<Heard> h = varied("vary double", "double = 1;", 1.3);

        if (h.size() != 6)
            fail("vary: double = 1 should say each of three notes twice; "
                 "heard " + std::to_string(h.size()));
        else if (!near(h[0].at, 0) || !near(h[0].dur, 0.125) ||
                 !near(h[1].at, 0.125) || !near(h[1].dur, 0.125) ||
                 h[1].note != h[0].note)
            fail("vary: a doubled note should be two halves of itself");
    }

    /* And the second half is still sounding a moment after it starts.
     *
     * The tape above says two notes whatever the scheduler then does
     * with them, which is why this is a check of its own. The off
     * derived for the first half falls due at exactly the instant the
     * second half is pressed, and a step that delivered its ons before
     * draining its offs released the note it had just made: thMidiChan
     * keys its voices by pitch, so the off found the new voice rather
     * than the one it was written for. What sounded was one note and a
     * release stub.
     *
     * Asserted on the voice, because the voice is the only place the
     * difference shows -- and through an instrument rather than a bare
     * channel, because a `channel = ' sink delivers to whatever graph is
     * loaded there and a scratch synth has none. */
    {
        clearChannels(synth);

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);
        std::string tmp = thUtil::tempFile("gencheck-double-");

        if (tmp.empty())
            fail("vary: the doubled-note check could not make a scratch "
                 "file");
        else
        {
            {
                std::ofstream out(tmp.c_str(), std::ios::trunc);

                /* One note, a second long, split into two halves that
                   meet at 0.5 s. Long enough that the instant they meet
                   is nowhere near either end of the note. */
                out << "seed 3;\n"
                    << "instrument pad { dsp \"amb01.dsp\"; };\n"
                    << "chain c {\n"
                    << "  stage src gen::euclid { steps = 1; fills = 1;"
                    << " rotate = 0;\n"
                    << "    notes = \"C4\"; period = 4 s; hold = 1 s;"
                    << " vel = 90; };\n"
                    << "  stage v xform::vary { grid = 0.5 s;"
                    << " double = 1; };\n"
                    << "  sink { instrument = pad; };\n"
                    << "};\n";

                if (!out.good())
                    fail("vary: the doubled-note check could not write " +
                         tmp);
            }

            const thcInstrument *pad = NULL;

            if (!loader.load(tmp, &sched))
            {
                for (size_t i = 0; i < loader.errors().size(); i++)
                    fprintf(stderr, "gencheck: %s\n",
                            loader.errors()[i].c_str());

                fail("vary: the doubled-note piece did not load");
            }
            else if ((pad = sched.instrument("pad")) == NULL)
                fail("vary: the doubled-note piece lost its instrument");
            else
            {
                sched.start();

                /* To 0.6 s: past the instant the halves meet, and well
                   short of where the second one ends. */
                for (int i = 0; i < 30; i++)
                {
                    sched.stepTransport(0.02);
                    drainSynth();
                }

                /* Read before stop(), which flushes the offs for
                   everything still sounding -- including the voice this
                   is about. */
                thMidiChan *c = synth->getChannel(pad->channel);
                thMidiNote *v = c != NULL ? c->getNote(60) : NULL;
                thNode *io = v != NULL ? v->synthTree()->IONode() : NULL;
                thArg *trigger = io != NULL ? io->getArg("trigger") : NULL;

                if (trigger == NULL)
                    fail("vary: a doubled note left no voice sounding at "
                         "all halfway through its second half");
                else if ((*trigger)[0] != 1)
                    fail("vary: the second half of a doubled note was "
                         "released the instant it began");

                sched.stop();
                drainSynth();
            }

            std::filesystem::remove(tmp);
        }

        clearChannels(synth);
    }

    {
        std::vector<Heard> h = varied("vary approach",
                                      "approach = 1;", 1.3);

        if (h.size() != 6)
            fail("vary: approach = 1 should put a tone before each of "
                 "three notes; heard " + std::to_string(h.size()));
        else
        {
            /* The neighbor is a scale step away, the note follows it,
               and between them they are the note that was written. */
            if ((h[0].note != 62 && h[0].note != 59) || h[1].note != 60)
                fail("vary: an approach should be the neighboring scale "
                     "degree, then the note");

            if (!near(h[0].at, 0) || !near(h[0].dur, 0.0625) ||
                !near(h[1].at, 0.0625) || !near(h[1].dur, 0.1875))
                fail("vary: an approach should take the front of the "
                     "note and no more");

            if (h[0].vel >= h[1].vel)
                fail("vary: an approach tone should be the lighter of "
                     "the two");
        }
    }

    {
        std::vector<Heard> h = varied("vary ornament",
                                      "ornament = 1;", 1.3);

        if (h.size() != 9)
            fail("vary: ornament = 1 should make three notes of each of "
                 "three; heard " + std::to_string(h.size()));
        else if (h[0].note != 60 || h[2].note != 60 ||
                 (h[1].note != 62 && h[1].note != 59))
            fail("vary: a mordent is the note, its neighbor, the note");
        else if (!near(h[0].at, 0) || !near(h[1].at, 0.0625) ||
                 !near(h[2].at, 0.125) || !near(h[2].dur, 0.125))
            fail("vary: a mordent should fit inside the note it decorates");
    }

    /* Same seed, same variation -- twice from the file, and again after
       a rewind, which is the claim every seeded piece here makes. */
    {
        std::string body = line;

        body.replace(body.find("%s"), 2,
                     "rest = 0.2; leap = 0.2; push = 0.2; double = 0.2;");

        const std::string first =
            renderBody(plugins, synth, "vary replay", body, 8.0);
        const std::string again =
            renderBody(plugins, synth, "vary replay 2", body, 8.0);

        if (first.empty() || first != again)
        {
            fail("vary: two renders of one seed composed differently");
            showDivergence(first, again, "first ", "second");
        }
    }

    /* A held note has no length to divide and no end to move, so it goes
       through whatever the knobs say -- and so does its release. */
    {
        const std::string path = thUtil::tempFile("gencheck-varyheld-");

        if (path.empty())
            fail("could not write the held-note vary piece");
        else
        {
            {
                std::ofstream out(path.c_str(), std::ios::trunc);

                out << "seed 3;\n"
                       "chain c {\n"
                       "  input midi;\n"
                       "  stage v xform::vary { rest = 1; };\n"
                       "  sink { channel = 1; };\n"
                       "};\n";
            }

            clearChannels(synth);
            drainSynth();

            thcScheduler sched(synth);
            thcGenLoader loader(plugins);

            if (!loader.load(path, &sched))
                fail("the held-note vary piece did not load");
            else
            {
                int ons = 0, offs = 0;
                sigc::connection conn = sched.sigDelivered.connect(
                    [&ons, &offs](const thcEvent &ev)
                    {
                        if (ev.type == THC_EV_NOTE)
                            ons++;
                        else if (ev.type == THC_EV_NOTEOFF)
                            offs++;
                    });

                sched.start();

                thcEvent ev = {};

                ev.type = THC_EV_NOTE;
        ev.u.note.level = 1;
                ev.at = sched.now();
                ev.channel = 0;
                ev.u.note.note = 60;
                ev.u.note.velocity = 100;
                ev.u.note.duration = 0;
                sched.injectMidiEvent(ev);

                sched.stepTransport(0.1);

                ev.type = THC_EV_NOTEOFF;
                ev.at = sched.now();
                sched.injectMidiEvent(ev);

                sched.stepTransport(0.1);
                sched.stop();
                conn.disconnect();
                drainSynth();

                if (ons != 1 || offs != 1)
                    fail("vary: a held note and its release should pass "
                         "through even at rest = 1; heard " +
                         std::to_string(ons) + " on and " +
                         std::to_string(offs) + " off");
            }

            remove(path.c_str());
        }
    }

    /* accent: the pattern's two levels in the right places. Four notes a
       second under `x.' on a half-second grid is strong, weak, strong,
       weak. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "accent pattern",
            "chain c {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
            "    notes = \"C4\"; period = 0.5 s; hold = 0.2 s;"
            "    vel = 90; };\n"
            "  stage a xform::accent { pattern = \"x.\"; grid = 0.5 s;\n"
            "    strong = 1.2; weak = 0.5; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 2.1);

        if (h.size() != 5)
            fail("accent: expected five notes in 2.1 s, got " +
                 std::to_string(h.size()));
        else if (h[0].vel != 108 || h[1].vel != 45 || h[2].vel != 108 ||
                 h[3].vel != 45 || h[4].vel != 108)
            fail("accent: `x.' should weight every other note, and did "
                 "not");
    }

    /* And the swell: a straight line across the bar, starting again at
       the top of the next one. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "accent swell",
            "chain c {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
            "    notes = \"C4\"; period = 0.5 s; hold = 0.2 s;"
            "    vel = 90; };\n"
            "  stage a xform::accent { bar = 2 s; from = 0.5; to = 1; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 2.1);

        if (h.size() != 5)
            fail("accent: expected five notes under the swell, got " +
                 std::to_string(h.size()));
        else if (h[0].vel != 45 || h[1].vel != 56 || h[2].vel != 68 ||
                 h[3].vel != 79 || h[4].vel != 45)
            fail("accent: the swell should climb across the bar and start "
                 "again at the next one");
    }

    /* euclid's fill: three cycles of the riff and a fourth of something
       else, and the riff picks up where it left off. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "euclid fill",
            "chain c {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
            "    notes = \"C4 D4 E4 F4 G4\"; fill = \"C5\"; every = 4;\n"
            "    period = 0.5 s; hold = 0.2 s; vel = 90; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 9.9);

        if (h.size() != 20)
            fail("euclid fill: expected twenty notes in five cycles, got " +
                 std::to_string(h.size()));
        else
        {
            for (size_t i = 12; i < 16; i++)
                if (h[i].note != 72)
                {
                    fail("euclid fill: the fourth cycle should come from "
                         "the fill pool");
                    break;
                }

            /* The riff's five notes against a ring of four: cycle 0 is
               C D E F, cycle 1 G C D E, cycle 2 F G C D, and cycle 4 --
               the one after the fill -- opens on D, which is the note it
               would have opened on had there been no fill at all. */
            if (h[0].note != 60 || h[4].note != 67 || h[8].note != 65 ||
                h[16].note != 62)
                fail("euclid fill: a fill should not move the pool under "
                     "the cycles around it");
        }
    }

    /* every = 0 is the default and never fills, whatever the pool says. */
    {
        std::vector<Heard> h = playBody(plugins, synth, "euclid no fill",
            "chain c {\n"
            "  stage src gen::euclid { steps = 4; fills = 4; rotate = 0;\n"
            "    notes = \"C4\"; fill = \"C5\";\n"
            "    period = 0.5 s; hold = 0.2 s; vel = 90; };\n"
            "  sink { channel = 1; };\n"
            "};\n", 9.9);

        for (size_t i = 0; i < h.size(); i++)
            if (h[i].note != 60)
            {
                fail("euclid fill: a fill pool with no `every' should "
                     "never be played");
                break;
            }
    }
}

/* A piece somebody plays rather than one that plays itself: chains
 * with `input midi' and no generator anywhere. Nothing to render. */
static bool
playedByHand (thcScheduler &sched)
{
    for (size_t ci = 0; ci < sched.chainCount(); ci++)
    {
        const thcChain *c = sched.chain(ci);

        if (c != NULL && !c->inputMidi)
            return false;
    }

    return true;
}

/* Where a shipped piece may be filed.
 *
 * gen/README.md groups the corpus by hand into these nine sections, and
 * that grouping is a real one -- somebody sat down and decided which piece
 * teaches what. What it was not is anywhere a program could see, so it
 * drifted: three pieces had come to be in no section at all, which is
 * exactly the failure this list turns into a gate.
 *
 * The *format* takes free text -- a piece of one's own may say whatever it
 * likes, and one that says nothing is Uncategorized rather than refused.
 * This is the discipline the shipped corpus is held to, and nothing else.
 * The same bargain scripts/dspcheck makes for a .dsp's category. */
static const char *const kCategories[] = {
    "Start here", "Playing it yourself", "Algorithms", "Timbre as material",
    "Pieces", "Game music", "The floor", "The eighties", "Disco", NULL
};

static bool
knownCategory (const std::string &category)
{
    for (int i = 0; kCategories[i] != NULL; i++)
        if (category == kCategories[i])
            return true;

    return false;
}

static std::string
categoryList (void)
{
    std::string out;

    for (int i = 0; kCategories[i] != NULL; i++)
        out += std::string(i ? ", " : "") + "'" + kCategories[i] + "'";

    return out;
}

static void
checkCorpus (const std::map<std::string, thcPlugin *> &plugins,
             thSynth *synth, const std::string &genFile)
{
    const std::vector<std::filesystem::path> files = piecesBeside(genFile);

    if (files.empty())
    {
        fail("no .gen files beside " + genFile + " -- the sweep swept "
             "nothing");
        return;
    }

    for (size_t i = 0; i < files.size(); i++)
    {
        const std::string leaf = files[i].filename().string();

        thcScheduler sched(synth);
        thcGenLoader loader(plugins);

        if (!loader.load(files[i].string(), &sched))
        {
            for (size_t k = 0; k < loader.errors().size(); k++)
                fprintf(stderr, "gencheck: %s\n",
                        loader.errors()[k].c_str());

            fail(leaf + " no longer loads");
            continue;
        }

        if (sched.chainCount() == 0)
        {
            fail(leaf + " loaded with no chains at all");
            continue;
        }

        /* And where it is filed. A shipped piece says, and says one of the
           eight; the README's sections are what a menu groups by now, so a
           piece in none of them is a piece nobody finds. */
        if (loader.pieceCategory().empty())
            fail(leaf + " declares no category (one of " + categoryList() +
                 ")");
        else if (!knownCategory(loader.pieceCategory()))
            fail(leaf + " is filed under '" + loader.pieceCategory() +
                 "', which is not one of " + categoryList());

        if (playedByHand(sched))
            continue;

        const std::string first = render(sched, 60.0, 0.05);

        if (first.empty())
            fail(leaf + " loads but delivers nothing in a minute");

        /* And again from the top, on every seeded piece and not only on
           the one checkReplay was handed: a rewind is a load, or it is
           not a replay. orrery used to fail this and airports never did,
           because what parted them was a composer whose param_changed
           reseeds -- gen::evolve -- and only the load announced its
           params (thcParamStore::rebind). Each peer in a jam presses
           Play as a rewind of the piece it loaded, so a rewind that
           composed anything else would part the peers from the first
           bar. */
        if (!loader.hasSeed())
            continue;

        sched.reset();

        const std::string again = render(sched, 60.0, 0.05);

        if (again != first)
        {
            fail(leaf + " composes differently after a rewind");
            showDivergence(first, again, "loaded", "rewound");
        }
    }
}

/* ---- a synth that never renders ----------------------------------------
 *
 * The composer view in the browser is a second scheduler over a second
 * synth, fed the commands the worklet is fed, holding real composer
 * instances for their pictures. That synth must not render -- there is no
 * audio thread behind it -- and must otherwise be the synth the scheduler
 * expects: instruments load, chanargs read back, channels come and go.
 * thSynth::setSilent is that, and this is the claim it rests on: every
 * seeded piece composes the same tape over a silent synth as over a
 * rendering one. A silent synth that dropped a SET_CHANNEL, or answered a
 * chanarg differently, would part the picture from the sound here, before
 * it ever parted them on a page.
 */
static void
checkSilent (const std::map<std::string, thcPlugin *> &plugins,
             thSynth *synth, thSynth *silent, const std::string &genFile)
{
    if (!silent->silent())
    {
        fail("the silent synth is not silent");
        return;
    }

    const std::vector<std::filesystem::path> files = piecesBeside(genFile);

    for (size_t i = 0; i < files.size(); i++)
    {
        const std::string leaf = files[i].filename().string();

        thcScheduler sounding(synth);
        thcScheduler quiet(silent);
        thcGenLoader loadA(plugins);
        thcGenLoader loadB(plugins);

        /* The corpus sweep already said whether it loads at all; what is
           asserted here is that it loads the same over both. */
        const bool okA = loadA.load(files[i].string(), &sounding);
        const bool okB = loadB.load(files[i].string(), &quiet);

        if (okA != okB)
        {
            fail(leaf + (okB ? " loads over a silent synth and not a "
                                "rendering one"
                              : " loads over a rendering synth and not a "
                                "silent one"));
            continue;
        }

        /* Unseeded, a piece draws its seed at load, and two loads are
           two pieces -- the same reason checkCorpus rewinds only the
           seeded ones. The jam plays seeded pieces, and so does this. */
        if (!okA || playedByHand(sounding) || !loadA.hasSeed())
            continue;

        /* The piece's instruments landed. A silent synth still parses
           the .dsp and installs the channel; only the notes stop at the
           door. Only the channels this piece names: the rendering synth
           is the one every check before this shares, and it is carrying
           whatever they left on it. */
        const std::vector<thcInstrument> &insts = quiet.instruments();

        for (size_t k = 0; k < insts.size(); k++)
        {
            const int ch = insts[k].channel;

            if (silent->getChannel(ch) == NULL)
                fail(leaf + ": instrument '" + insts[k].name +
                     "' did not land on channel " + std::to_string(ch) +
                     " of the silent synth");
        }

        const std::string heard = render(sounding, 60.0, 0.05);
        const std::string mirrored = render(quiet, 60.0, 0.05);

        if (heard != mirrored)
        {
            fail(leaf + " composes differently over a silent synth");
            showDivergence(heard, mirrored, "sounding", "silent  ");
        }
    }

    /* The reason the mode exists: a synth stepped without rendering
       dropped commands within one fast-forward. Twenty pieces of a
       minute each, over a ring drained as a mirror drains it, and
       nothing may have fallen off. */
    if (silent->droppedCommands() != 0)
        fail("the silent synth dropped " +
             std::to_string(silent->droppedCommands()) +
             " commands over the sweep");

    /* And it kept its word about the sound. */
    const float *out = silent->getOutput();
    const size_t samples = (size_t)silent->audioChannelCount() *
                           (size_t)silent->getWindowlen();

    for (size_t i = 0; out != NULL && i < samples; i++)
        if (out[i] != 0.0f)
        {
            fail("the silent synth rendered something");
            break;
        }
}

/* ----------------------------------------------------------------------- */

int
main (int argc, char *argv[])
{
    Glib::init();

    std::string pluginDir;
    std::string genFile;
    bool json = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            pluginDir = argv[++i];
        else if (strcmp(argv[i], "--json") == 0)
            json = true;
        else
            genFile = argv[i];
    }

    /* The piece list a menu is drawn from, printed, checking nothing.
     *
     * The other half of a parity gate: the browser's module scans its own
     * copy of the same directory through the same class and prints the same
     * bytes, and wasm/web/gencatalogcheck.mjs diffs the two. The argument is
     * a directory here rather than a file, and no plugins are needed --
     * reading a header does not run a piece. */
    if (json)
    {
        GenCatalog catalog;

        catalog.scan(genFile);

        printf("%s\n", genCatalogToJson(catalog).c_str());

        return 0;
    }

    if (pluginDir.empty() || genFile.empty())
    {
        fprintf(stderr, "usage: gencheck -p <plugindir> <file.gen>\n"
                "       gencheck --json <gendir>\n");
        return 2;
    }

    checkNotes();

    std::map<std::string, thcPlugin *> plugins;

    loadComposers(pluginDir, plugins);

    if (plugins.find("eno_line") == plugins.end() ||
        plugins.find("quantize") == plugins.end() ||
        plugins.find("walk") == plugins.end())
    {
        fprintf(stderr, "gencheck: composer modules missing from %s -- "
                "build the plugins first\n", pluginDir.c_str());
        return 2;
    }

    /* Built with the plugin path rather than bare, because a piece can
       now carry its own instrument and "the instrument loaded" means a
       .dsp parsed and dlopen'd its nodes. A synth with nowhere to find
       them would make checkInstruments pass by failing to try. */
    thSynth synth(pluginDir, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    tapeSynth = &synth;

    /* The mirror's kind of synth, beside the real one: see checkSilent.
       Made silent before anything is loaded on it, which is the one
       rule setSilent has. */
    thSynth silent(pluginDir, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    silent.setSilent(true);
    silentSynth = &silent;

    checkValidation(plugins, &synth);
    checkExpressions(plugins, &synth);
    checkReplay(plugins, &synth, genFile);
    checkLiveEdits(plugins, &synth, genFile);
    checkPlanners(plugins, &synth);
    checkLiveInput(plugins, &synth);
    checkEdits(plugins, &synth, genFile);
    checkPresets(plugins, &synth);
    checkInput(plugins, &synth);
    checkGrid(plugins, &synth);
    checkTempoAndRevival(plugins, &synth);
    checkInstruments(plugins, &synth);
    checkInstrumentEffects(plugins, &synth);
    checkEffectSide(plugins, &synth);
    checkEffectChanargSink(plugins, &synth);
    checkNodes(plugins, &synth, genFile);
    checkStructureEdits(plugins, &synth, genFile);
    checkColony(plugins, &synth, genFile);
    checkPhrasing(plugins, &synth);
    checkHarmonyKit(plugins, &synth);
    checkVoiceLeading(plugins, &synth);
    checkHeldNotes(plugins, &synth);
    checkFloor(plugins, &synth);
    checkSections(plugins, &synth);
    checkChainStart(plugins, &synth);
    checkRun(plugins, &synth);
    checkVariation(plugins, &synth);
    checkMasterEffect(plugins, &synth);
    checkCorpus(plugins, &synth, genFile);
    checkSilent(plugins, &synth, &silent, genFile);

    /* Freed for the leak checker's sake, not the OS's: a gate that
       runs under sanitizers should not salt the report. The schedulers
       are already gone -- each check scoped its own. */
    for (std::map<std::string, thcPlugin *>::iterator i = plugins.begin();
         i != plugins.end(); ++i)
        delete i->second;

    if (failures == 0)
        printf("gencheck: OK\n");
    else
        printf("gencheck: %d failure%s\n", failures,
               failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
