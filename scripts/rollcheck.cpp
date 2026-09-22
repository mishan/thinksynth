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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * rollcheck -- the piano roll's drawing, with no window around it.
 *
 *     scripts/rollcheck -p build/plugins/ gen/airports.gen
 *     scripts/rollcheck -p build/plugins/ --dump gen/airports.gen
 *
 * RollCanvas is the desktop's piano roll with the widget taken off
 * (src/RollCanvas.h), and the browser compiles the same file and draws
 * with it in the mirror worker. Two claims follow from that, and this
 * harness is both of them.
 *
 * The first is that the drawing says what the roll is for. Half of this
 * widget is the *scheduled future* -- what the piece has already decided
 * and not yet played -- and the page's old roll could not show it,
 * because a tape is what has been delivered. A list of ops is exactly
 * where that can be asserted without a screenshot: outlined rectangles,
 * to the right of the now-line, in a piece that is running.
 *
 * The second is that the two builds draw the same picture. `--dump'
 * prints the list the native class produced -- the ops and the strings
 * they index -- and wasm/web/rollcheck.mjs plays the same seeded piece to
 * the same frame through the module and diffs the two, the way
 * nodecheck.mjs diffs an edit against dspedit's. A seeded piece replays
 * identically (docs/GEN_FORMAT.md, gencheck), and the roll's drawing at a
 * given frame is a function of the piece and the frame, so anything left
 * over is the two builds disagreeing.
 *
 * Which is why this binary records rather than rasterises: it is linked
 * against cairo-canvas2d, the same cairo the browser module draws
 * through, instead of against real cairo. Nothing else in the native tree
 * is, and scripts/CMakeLists.txt says what that costs.
 *
 * The transport is stepped the way the module steps it -- once per
 * window, to the time that window's last frame falls on -- because the
 * comparison is only worth anything if both sides ask the roll to draw
 * the same instant of the same piece.
 *
 * Exit status is the number of failures.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <glibmm.h>

#include "think.h"

#include "cairo2d.h"
#include "cairomm/context.h"

#include "libthink/thSynthCommand.h"
#include "thcPlugin.h"
#include "thcGenFile.h"
#include "thcScheduler.h"

#include "RollCanvas.h"

/* The window the browser's page opens its synth with, and the rate. The
   transport is stepped in these, so both sides reach the same instant by
   the same arithmetic rather than by two roundings of one number. */
static const int    WINDOW = 256;
static const int    RATE   = 48000;

/* How much of the piece is played before the roll is asked to draw, and
   how many frames it is then drawn for. Long enough that there is history
   behind the now-line and a queue in front of it; the frames matter
   because the pitch range eases toward its fit one draw at a time, so
   "the same frame" means the same number of pictures as well as the same
   instant. */
static const double SECONDS = 8.0;
static const int    FRAMES  = 30;

/* And how long the piece is played for the prune check at the end, which
   wants a transport well past four times the shortest span the roll can
   be set to -- 20s of scrub window, so 40s of playing leaves half the
   run behind the cutoff. Not part of the comparison, so it costs the
   diff nothing. */
static const double LONG_SECONDS = 40.0;

/* The size the comparison draws at. Not square, because the roll is a
   band: a shape it will never be asked for is a shape whose arithmetic
   nobody would notice being wrong. */
static const int    WIDTH  = 900;
static const int    HEIGHT = 180;

static int failures = 0;

static void
check (bool good, const std::string &what)
{
    printf("%s  %s\n", good ? "ok  " : "FAIL", what.c_str());

    if (!good)
        failures++;
}

/* Every composer module in the tree, by name. The same scan gencheck
   makes, and for the same reason: a piece names its stages by plugin
   name and nothing else can resolve one. */
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
    }
}

/* ---- playing a piece the way the module plays one --------------------- */

/* The scheduler stepped to `seconds', one window at a time.
 *
 * thinkweb.cpp's step(): the target is the transport time the window's
 * last frame falls on, counted from the origin, and by nothing measured
 * -- so the piece is a function of the file and the seed. The same
 * sequence of targets here is what makes the two drawings comparable.
 *
 * The synth is drained every so often for gencheck's reason: every
 * delivered note posts a command for an audio thread that is not here,
 * and a full ring drops the SET_CHANNEL a piece's own instrument arrives
 * on. */
static void
play (thcScheduler &sched, thSynth &synth, double seconds)
{
    const double step = (double)WINDOW / RATE;
    long windows = 0;

    sched.start();

    for (long k = 1; sched.now() < seconds && sched.running(); k++)
    {
        sched.stepTransportTo(k * step);

        if (++windows % 64 == 0)
            synth.process();
    }

    synth.process();
}

/* ---- the list, out of cairo2d's recorder ------------------------------ */

/* Everything drawn since the context was made, as the three tables the
   page reads: no surfaces, because the roll blits nothing. */
struct List
{
    std::vector<float>       ops;
    std::vector<std::string> strings;
};

static List
listOf (cairo_t *cr)
{
    List out;
    const float *ops = cairo2d_ops(cr);
    const int words = cairo2d_op_words(cr);

    for (int i = 0; i < words; i++)
        out.ops.push_back(ops[i]);

    for (int i = 0; i < cairo2d_string_count(cr); i++)
        out.strings.push_back(cairo2d_string(cr, i));

    return out;
}

/* The roll drawn for `frames' frames, with the list from the last one.
 *
 * One recorder and one cairomm face over it, restarted per frame, which
 * is exactly what the module does (wasm/web/twdraw.cpp): the list is read
 * out before the next draw begins, on both sides. Two of them, or a fresh
 * context per frame, would be two arrangements being compared rather than
 * one drawing. */
static List
drawRoll (RollCanvas &roll, int width, int height, int frames)
{
    static cairo_t *cr = NULL;
    static Cairo::RefPtr<Cairo::Context> ctx;
    List last;

    if (cr == NULL)
    {
        cr = cairo2d_create();
        ctx = Cairo::Context::create(cr);
    }

    for (int i = 0; i < frames; i++)
    {
        cairo2d_begin(cr);
        roll.draw(ctx, width, height);
        last = listOf(cr);
    }

    return last;
}

/* The dump wasm/web/rollcheck.mjs reads: JSON, because the other side is
 * JavaScript, and %.9g because that round-trips a float exactly.
 *
 * The run's own numbers go out with it -- how long the piece was played
 * for, at what window and rate, how many frames it was drawn for and how
 * big -- so the other side drives itself from these rather than from a
 * second copy of them that could quietly stop matching. Every one of them
 * has to agree or the two lists are of two different instants. */
static void
dump (const List &list)
{
    printf("{\"seconds\":%g,\"frames\":%d,\"width\":%d,\"height\":%d,"
           "\"window\":%d,\"rate\":%d,",
           SECONDS, FRAMES, WIDTH, HEIGHT, WINDOW, RATE);
    printf("\"ops\":[");

    for (size_t i = 0; i < list.ops.size(); i++)
        printf("%s%.9g", i ? "," : "", (double)list.ops[i]);

    printf("],\"strings\":[");

    for (size_t i = 0; i < list.strings.size(); i++)
    {
        printf("%s\"", i ? "," : "");

        for (const char c : list.strings[i])
            if (c == '"' || c == '\\')
                printf("\\%c", c);
            else if ((unsigned char)c < 0x20)
                printf("\\u%04x", c);
            else
                printf("%c", c);

        printf("\"");
    }

    printf("]}\n");
}

/* ---- what the list has to have in it ---------------------------------- */

/* cairo2d's opcodes, as much of them as this needs. The arity table is
 * the package's and is walked here the way drawcheck.mjs walks it in
 * JavaScript: an op the table does not know, or a list that does not end
 * where the table says it ends, is a list the page would replay as
 * operands read as opcodes.
 */
static bool
walk (const std::vector<float> &ops, std::string &why, int &count)
{
    count = 0;

    for (size_t i = 0; i < ops.size(); )
    {
        const int op = (int)ops[i++];
        int arity = cairo2d_op_arity(op);

        if (cairo2d_op_name(op) == NULL)
        {
            why = "op " + std::to_string(op) + " at word " +
                  std::to_string(i - 1) + " is not one";
            return false;
        }

        if (arity < 0)                  /* SET_DASH says its own length */
        {
            if (i >= ops.size())
            {
                why = "a dash at word " + std::to_string(i - 1) +
                      " has no count";
                return false;
            }

            arity = (int)ops[i] + 2;
        }

        if (i + (size_t)arity > ops.size())
        {
            why = "op " + std::to_string(op) + " at word " +
                  std::to_string(i - 1) + " runs off the end";
            return false;
        }

        i += (size_t)arity;
        count++;
    }

    return true;
}

int
main (int argc, char *argv[])
{
    Glib::init();

    std::string pluginDir, genFile;
    bool dumping = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            pluginDir = argv[++i];
        else if (strcmp(argv[i], "--dump") == 0)
            dumping = true;
        else
            genFile = argv[i];
    }

    if (pluginDir.empty() || genFile.empty())
    {
        fprintf(stderr,
                "usage: rollcheck -p <plugindir> [--dump] <file.gen>\n");
        return 2;
    }

    std::map<std::string, thcPlugin *> plugins;

    loadComposers(pluginDir, plugins);

    if (plugins.empty())
    {
        fprintf(stderr, "rollcheck: no composer modules in %s -- build "
                "the plugins first\n", pluginDir.c_str());
        return 2;
    }

    thSynth synth(pluginDir, WINDOW, RATE);
    thcScheduler sched(&synth);
    thcGenLoader loader(plugins);

    if (!loader.load(genFile, &sched))
    {
        fprintf(stderr, "rollcheck: %s did not load\n", genFile.c_str());

        for (size_t i = 0; i < loader.errors().size(); i++)
            fprintf(stderr, "  %s\n", loader.errors()[i].c_str());

        return 2;
    }

    /* Built after the load, as the mirror's is: what it keeps is what
       the scheduler delivers from here on, and the instrument
       applications a load itself delivers are not part of the piece. */
    RollCanvas roll(&sched);

    play(sched, synth, SECONDS);

    const List list = drawRoll(roll, WIDTH, HEIGHT, FRAMES);

    if (dumping)
    {
        dump(list);
        return 0;
    }

    /* ---- the list is one ---- */

    std::string why;
    int count = 0;
    const bool walked = walk(list.ops, why, count);

    check(walked,
          walked ? std::filesystem::path(genFile).filename().string() +
                   " at " + std::to_string((int)SECONDS) + "s is " +
                   std::to_string(count) + " ops in " +
                   std::to_string(list.ops.size()) + " words"
                 : why);

    /* ---- and the future is in it ----
     *
     * The assertion this whole extraction is for. A delivered note is a
     * filled rectangle and a scheduled one is an outline, so what says
     * the future half is being drawn is a stroked rectangle to the right
     * of the now-line -- and the now-line is at spanPast of spanPast plus
     * spanFuture across the width, which is the only place in the drawing
     * that ratio is spelled twice.
     *
     * Read out of the list rather than out of the canvas's own state,
     * because what is being checked is the *drawing*: a roll that kept a
     * perfect pending queue and drew none of it would pass every other
     * question here.
     */
    const double nowX = WIDTH * roll.spanPast() /
                        (roll.spanPast() + roll.spanFuture());
    int ahead = 0, behind = 0;
    bool rect = false;
    double rectX = 0;

    for (size_t i = 0; i < list.ops.size(); )
    {
        const int op = (int)list.ops[i++];
        int arity = cairo2d_op_arity(op);

        if (arity < 0)
            arity = (int)list.ops[i] + 2;

        if (op == CAIRO2D_RECTANGLE)
        {
            rect = true;
            rectX = list.ops[i];
        }
        else if (op == CAIRO2D_STROKE)
        {
            if (rect && rectX > nowX)
                ahead++;

            rect = false;
        }
        else if (op == CAIRO2D_FILL)
        {
            if (rect && rectX < nowX)
                behind++;

            rect = false;
        }

        i += (size_t)arity;
    }

    check(ahead > 0,
          "the scheduled future is drawn: " + std::to_string(ahead) +
          " outlined notes right of the now-line at x=" +
          std::to_string((int)nowX));

    check(behind > 0,
          "and the past is drawn: " + std::to_string(behind) +
          " filled notes left of it");

    /* ---- what a gesture does to the view ----
     *
     * The shell hands pixels in and the content decides what they mean,
     * on every platform, which is the one thing about a canvas that can
     * be wrong silently -- a drag that lands somewhere else is invisible
     * until somebody tries it. Public entry points, so ctest can try it.
     */
    const double was = roll.viewNow();

    roll.pressAt(WIDTH * 0.4, HEIGHT / 2.0, 1, 1);
    roll.motionTo(WIDTH * 0.5, HEIGHT / 2.0);
    roll.releaseAt(WIDTH * 0.5, HEIGHT / 2.0, 1);

    check(!roll.following() && roll.viewNow() < was,
          "a drag to the right scrubs back and drops out of follow");

    roll.pressAt(WIDTH * 0.5, HEIGHT / 2.0, 1, 2);

    check(roll.following(), "and a double-click goes back to live");

    roll.releaseAt(WIDTH * 0.5, HEIGHT / 2.0, 1);

    /* A motion with nothing held is a pointer crossing the roll, not a
       scrub. The browser sends one for every mouse move over the canvas
       and the desktop's motion controller does the same. */
    roll.motionTo(WIDTH * 0.1, HEIGHT / 2.0);

    check(roll.following(),
          "and a motion with nothing held moves nothing");

    /* The wheel is time. A factor above one means "draw it bigger",
       which for this canvas is less time on screen -- the one place a
       shell's number is read as something other than its name
       (RollCanvas::zoomBy). */
    const double span = roll.spanPast();

    roll.zoomBy(1.25);
    check(roll.spanPast() < span,
          "a wheel in shows less time: " + std::to_string((int)span) +
          "s -> " + std::to_string((int)roll.spanPast()) + "s");

    for (int i = 0; i < 40; i++)
        roll.zoomBy(0.8);

    check(roll.spanPast() >= 5.0 && roll.spanFuture() >= 2.5,
          "and forty notches out stop at the bounds: " +
          std::to_string((int)roll.spanPast()) + "s and " +
          std::to_string((int)roll.spanFuture()) + "s");

    /* ---- and a rewind takes the piece with it ----
     *
     * History is keyed to transport time, so notes kept across a rewind
     * sit right of the new now-line and draw as a future that already
     * happened. That is what showed up when the desktop switched pieces,
     * and it is why this class listens to sigReset at all. */
    sched.reset();

    const List after = drawRoll(roll, WIDTH, HEIGHT, 1);

    check(after.ops.size() < list.ops.size() && roll.following() &&
          roll.viewNow() == 0,
          "a rewind drops the history: " + std::to_string(list.ops.size()) +
          " words -> " + std::to_string(after.ops.size()));

    /* ---- and a bar with no end yet does not freeze the history ----
     *
     * A note delivered with duration <= 0 is live input's "held until
     * further notice", and its bar has no end until a NOTEOFF gives it
     * one. The roll used to keep those in the same queue as the ended
     * ones, where prune() walks from the front and must not drop a bar
     * that is still sounding -- so one of them at the front stopped the
     * walk, and every note behind it stayed for as long as the key was
     * down. A flushed key (thcScheduler::stop) meant forever.
     *
     * Asked of the canvas and not of the drawing, which is the one
     * thing here that cannot be read out of an op list: a bar older
     * than the window is off screen and skipped whether it was pruned
     * or not, so the leak and the fix draw the same picture.
     *
     * The note goes in through sigDelivered rather than through a key,
     * because what is being tested is the roll's own bookkeeping and
     * belfry has no `input midi' chain to press a key on. */
    roll.SetTimeSpan(5, 2.5);

    thcEvent down;

    down.type            = THC_EV_NOTE;
    down.at              = sched.now();
    down.channel         = 0;
    down.u.note.note     = 60;
    down.u.note.velocity = 100;
    down.u.note.duration = 0;          /* held: no end, ever            */

    sched.sigDelivered.emit(down);

    play(sched, synth, LONG_SECONDS);

    const double cutoff = sched.now() - 4 * roll.spanPast();

    check(roll.oldestKept() >= cutoff,
          "a bar still sounding does not freeze the history: at " +
          std::to_string((int)sched.now()) + "s the oldest kept ends at " +
          std::to_string((int)roll.oldestKept()) + "s, cutoff " +
          std::to_string((int)cutoff) + "s");

    printf("\n%s\n", failures == 0
           ? "the roll draws what it has played and what it is about to"
           : (std::to_string(failures) + " failed").c_str());

    return failures;
}
