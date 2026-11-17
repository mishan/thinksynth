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
 * dspwrite -- exercises NodeEdit against every settable value in the corpus.
 *
 * This is the check that matters most in the whole node editor, because the
 * writer is the only part that can destroy someone's work. For every plain
 * numeric parameter of every node of every .dsp, on a copy:
 *
 *   1. rewrite it to a new value, reparse, and confirm the parser sees exactly
 *      that value -- so `5 ms' comes back as a millisecond value and not as a
 *      folded raw number
 *   2. confirm exactly one line of the file changed
 *   3. write the *current* value -- an edit that changes nothing -- and
 *      confirm the file is byte identical
 *   4. write the original value back and confirm it parses to the original
 *      value again, in at most one changed line
 *
 * And for every control -- the .dsp's top-level `@name' blocks:
 *
 *   8. move it, reparse, confirm the value took
 *   9. put it back, confirm at most one line differs, and confirm a write of
 *      the value already there changes nothing at all
 *  10. write its *existing* range, label and group back over itself, and
 *      confirm no byte of the file moved
 *  11. widen the range and give it a label and a group whether or not it had
 *      them, reparse, and confirm all four took -- and that the value is still
 *      inside the range the file now declares
 *  12. put the metadata back, and confirm the file is byte identical. Stronger
 *      than (9) can be: every line involved either existed and was rewritten
 *      in place, or did not and is deleted again, so a label the file never
 *      had must leave no trace of having been added
 *  13. narrow the range past the value, and confirm the value moved into it.
 *      Separate from (11) because a clamp is one-way: a range widened and put
 *      back leaves the file as it was, and a range narrowed and put back
 *      cannot
 * And for every node and every control, one at a time, from a fresh copy:
 *  14. remove it, and confirm what is left still parses. Removing one rewrites
 *      every reference to it, wherever a statement puts that reference, and
 *      a reference left behind names something that is not there -- a file
 *      that no longer loads, in either editor
 *  16. rename it, and confirm the parse is the same graph under the new
 *      name; rename it back and confirm the file is byte identical; rename
 *      it onto another's name and confirm that is refused
 *
 * And for every expression:
 *  17. set it to the file's own text of it, and confirm that is the file or,
 *      joined onto one line, the same graph; set it to twice that, and
 *      confirm it parses and reads back as written
 *
 * And for every header field -- name, author, description, category:
 *  15. set it to something new, reparse and confirm the tree says it; put it
 *      back and confirm the file is byte identical; take it out and put it
 *      back in, and confirm the same -- a field added goes where the corpus
 *      keeps it
 *
 * And for every wire:
 *
 *   5. disconnect it, reparse, confirm the parameter is no longer driven
 *   6. reconnect it and confirm the file is byte identical to where it started
 *   7. connect it again to where it already goes, and confirm nothing moved
 *
 * (3) is the strong one, and it is the guarantee the editor actually makes:
 * open a file, save it, nothing changed. Not the same as (4). Once a value has
 * genuinely been changed and changed back, `inmax = th_max' comes back as
 * `inmax = 1' -- the writer has no memory of how a number used to be spelled,
 * only of when it does not need to touch one at all. 229 uses of th_max and
 * th_min across the corpus survive (3), which is what matters, and would not
 * survive a writer that regenerated the line every time.
 *
 *   make -C scripts
 *   LD_LIBRARY_PATH=libthink scripts/dspwrite -p plugins/ $(find dsp -name '*.dsp')
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "thUtil.h"

#include "think.h"
#include "NodeGraph.h"
#include "NodeEdit.h"
#include "NodeLayout.h"

static bool slurp (const string &path, string &out)
{
    ifstream in(path.c_str(), ios::binary);

    if (!in)
        return false;

    out.assign((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());

    return true;
}

static bool spit (const string &path, const string &text)
{
    ofstream out(path.c_str(), ios::binary | ios::trunc);

    if (!out)
        return false;

    out << text;

    return out.good();
}

/* An exit status is eight bits, and a count of failures is not: a run
   with exactly 256 of them exits 0 and the gate that reads the status
   calls it a pass. Clamped below 126, which the shell keeps for its
   own. */
static int exitCode (int failures)
{
    if (failures <= 0)
        return 0;

    return failures > 125 ? 125 : failures;
}

/* ---- both ways at once --------------------------------------------------
 *
 * Every edit NodeEdit offers comes in two: one over a file, which is what
 * the desktop does, and one over the file's text, which is what a browser
 * tab does -- there the document *is* the patch and there is no file at
 * all.
 *
 * The two cannot drift apart, and it is worth being exact about why: the
 * file overloads *are* the text ones, wrapped. NodeEdit::setValue is
 * overFile(readText, Text::setValue, writeText) and so is every other, so
 * an agreement check on the bytes and the Result is checking that a
 * function equals itself. The sweep still goes through these, because
 * running every one of the forty call sites down both entry points is
 * what would catch the day somebody gives one of them an implementation
 * of its own -- but that is a guard, not a test, and it is not what
 * earns the count these print.
 *
 * What does is the wrapper. overFile has exactly two promises the text
 * edit underneath it cannot make, and neither shows up in a byte
 * comparison:
 *
 *   - an edit that changes no byte writes nothing at all. "Open a file
 *     and save it, nothing changed" is the guarantee the editor makes,
 *     and a file's mtime, its permissions and the inode an editor is
 *     watching are all part of "nothing".
 *   - the write is atomic, a temporary renamed into place, so a failure
 *     leaves the old file and never a prefix of the new one -- and never
 *     a `.edit-tmp' beside it either.
 *
 * Both are checked below, on every one of those forty call sites.
 */
namespace both {

static int disagreements = 0;
static int checked = 0;
static int suppressed = 0;

/* The file's identity rather than its contents. writeText renames a
   temporary over the target, so a file that was rewritten comes back with
   a new inode and one that was left alone keeps the old one -- which is
   the only way to see a write that changed nothing, since by definition
   its bytes are the same either way.

   Zero where there is no such thing to ask for, which turns the check off
   rather than failing it: Windows' _stat gives st_ino no meaning. */
static unsigned long long identity (const string &path)
{
#ifndef _WIN32
    struct stat st;

    if (stat(path.c_str(), &st) == 0)
        return (unsigned long long)st.st_ino;
#else
    (void)path;
#endif

    return 0;
}

static NodeEdit::Result agree (const char *what, const string &file,
                               const string &was, unsigned long long ino,
                               NodeEdit::Result fileR,
                               const string &fileWhy, NodeEdit::Result textR,
                               const string &textWhy, const string &text)
{
    string after;

    slurp(file, after);
    checked++;

    /* The two answers first -- a guard against the day the file overload
       stops being the text one wrapped, cheap because it is already
       here. */
    if (fileR != textR)
        printf("FAIL  %s over text answered %s, over a file %s\n", what,
               NodeEdit::resultText(textR), NodeEdit::resultText(fileR));
    else if (fileWhy != textWhy)
        printf("FAIL  %s said \"%s\" over text and \"%s\" over a file\n",
               what, textWhy.c_str(), fileWhy.c_str());
    else if (after != text)
        printf("FAIL  %s wrote %zu bytes over a file and %zu over text\n",
               what, after.size(), text.size());

    /* And then the wrapper's own two promises, which are what this is
       really for. An edit that moved no byte must not have touched the
       file: same bytes, same file. */
    else if (ino != 0 && text == was && identity(file) != ino)
        printf("FAIL  %s changed no byte of %s and rewrote it anyway\n",
               what, file.c_str());

    /* And the write is a temporary renamed into place, which leaves
       nothing beside the target whether it worked or not. */
    else if (std::filesystem::exists(file + ".edit-tmp"))
        printf("FAIL  %s left %s.edit-tmp behind\n", what, file.c_str());

    else
    {
        if (ino != 0 && text == was)
            suppressed++;

        return fileR;
    }

    disagreements++;

    return fileR;
}

static NodeEdit::Result setValue (const string &file, const string &node,
                                  const string &arg, double value,
                                  string &why)
{
    string text, textWhy;

    slurp(file, text);

    const string was = text;
    const unsigned long long ino = identity(file);
    const NodeEdit::Result t = NodeEdit::Text::setValue(text, node, arg, value,
                                                  textWhy);
    const NodeEdit::Result r = NodeEdit::setValue(file, node, arg, value, why);

    return agree("setValue", file, was, ino, r, why, t, textWhy, text);
}

static NodeEdit::Result setChanArg (const string &file, const string &name,
                                    double value, string &why)
{
    string text, textWhy;

    slurp(file, text);

    const string was = text;
    const unsigned long long ino = identity(file);
    const NodeEdit::Result t = NodeEdit::Text::setChanArg(text, name, value,
                                                    textWhy);
    const NodeEdit::Result r = NodeEdit::setChanArg(file, name, value, why);

    return agree("setChanArg", file, was, ino, r, why, t, textWhy, text);
}

static NodeEdit::Result setControlMeta (const string &file,
                                        const string &name, double min,
                                        double max, const string &label,
                                        const string &group, string &why)
{
    string text, textWhy;

    slurp(file, text);

    const string was = text;
    const unsigned long long ino = identity(file);
    const NodeEdit::Result t = NodeEdit::Text::setControlMeta(text, name, min, max,
                                                        label, group,
                                                        textWhy);
    const NodeEdit::Result r = NodeEdit::setControlMeta(file, name, min, max,
                                                        label, group, why);

    return agree("setControlMeta", file, was, ino, r, why, t, textWhy, text);
}

static NodeEdit::Result disconnect (const string &file, const string &node,
                                    const string &arg, double value,
                                    string &why)
{
    string text, textWhy;

    slurp(file, text);

    const string was = text;
    const unsigned long long ino = identity(file);
    const NodeEdit::Result t = NodeEdit::Text::disconnect(text, node, arg, value,
                                                    textWhy);
    const NodeEdit::Result r = NodeEdit::disconnect(file, node, arg, value,
                                                    why);

    return agree("disconnect", file, was, ino, r, why, t, textWhy, text);
}

static NodeEdit::Result connect (const string &file, const string &node,
                                 const string &arg, const string &srcNode,
                                 const string &srcPort, string &why)
{
    string text, textWhy;

    slurp(file, text);

    const string was = text;
    const unsigned long long ino = identity(file);
    const NodeEdit::Result t = NodeEdit::Text::connect(text, node, arg, srcNode,
                                                 srcPort, textWhy);
    const NodeEdit::Result r = NodeEdit::connect(file, node, arg, srcNode,
                                                 srcPort, why);

    return agree("connect", file, was, ino, r, why, t, textWhy, text);
}

static NodeEdit::Result connectControl (const string &file,
                                        const string &node, const string &arg,
                                        const string &control, string &why)
{
    string text, textWhy;

    slurp(file, text);

    const string was = text;
    const unsigned long long ino = identity(file);
    const NodeEdit::Result t = NodeEdit::Text::connectControl(text, node, arg,
                                                        control, textWhy);
    const NodeEdit::Result r = NodeEdit::connectControl(file, node, arg,
                                                        control, why);

    return agree("connectControl", file, was, ino, r, why, t, textWhy, text);
}

/* Read-only, so there is nothing to compare but the answer. */
static NodeEdit::Result find (const string &file, const string &node,
                              const string &arg)
{
    string text;

    slurp(file, text);

    const NodeEdit::Result t = NodeEdit::Text::find(text, node, arg);
    const NodeEdit::Result r = NodeEdit::find(file, node, arg);

    checked++;

    if (t != r)
    {
        printf("FAIL  find over text answered %s, over a file %s\n",
               NodeEdit::resultText(t), NodeEdit::resultText(r));
        disagreements++;
    }

    return r;
}

} /* namespace both */

static int changedLines (const string &a, const string &b)
{
    vector<string> la, lb;

    for (int which = 0; which < 2; which++)
    {
        const string &s = which ? b : a;
        vector<string> &v = which ? lb : la;

        string cur;

        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '\n') { v.push_back(cur); cur.clear(); }
            else cur += s[i];
        }

        if (!cur.empty())
            v.push_back(cur);
    }

    /* A line inserted in the middle would make every subsequent line look
       changed under a positional comparison, so walk both sides together and
       let one skip. Enough for the one-line edits this writer makes; not a
       general diff. */
    size_t i = 0, j = 0;
    int n = 0;

    while (i < la.size() && j < lb.size())
    {
        if (la[i] == lb[j]) { i++; j++; continue; }

        /* an insertion in b */
        if (i < la.size() && j + 1 < lb.size() && la[i] == lb[j + 1])
        { n++; j++; continue; }

        /* a deletion from a */
        if (i + 1 < la.size() && j < lb.size() && la[i + 1] == lb[j])
        { n++; i++; continue; }

        n++; i++; j++;
    }

    n += (int)((la.size() - i) + (lb.size() - j));

    return n;
}

static void showFirstDiff (const string &a, const string &b)
{
    vector<string> la, lb;

    for (int which = 0; which < 2; which++)
    {
        const string &s = which ? b : a;
        vector<string> &v = which ? lb : la;
        string cur;

        for (size_t i = 0; i < s.size(); i++)
        { if (s[i] == '\n') { v.push_back(cur); cur.clear(); } else cur += s[i]; }

        if (!cur.empty()) v.push_back(cur);
    }

    for (size_t i = 0; i < la.size() && i < lb.size(); i++)
        if (la[i] != lb[i])
        { printf("        was: [%s]\n        now: [%s]\n",
                 la[i].c_str(), lb[i].c_str());
          return; }
}

/* Reads back one arg's value by parsing the file again. */
static bool readValue (thSynth &synth, const string &path, const string &node,
                       const string &arg, float &out)
{
    thSynthTree *tree = synth.parseTree(path);

    if (tree == NULL)
        return false;

    bool ok = false;

    thNode *n = tree->findNode(node);

    if (n)
    {
        thArg *a = n->getArg(arg);

        if (a && a->type() == thArg::ARG_VALUE && a->len() > 0)
        {
            out = (*a)[0];
            ok = true;
        }
    }

    delete tree;

    return ok;
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;
    bool quiet = false;
    int firstFile = -1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p")) { if (++i >= argc) return 2; pluginPath = argv[i]; }
        else if (!strcmp(argv[i], "-q")) quiet = true;
        else { firstFile = i; break; }
    }

    if (firstFile < 0)
    {
        printf("usage: %s [-p PATH] [-q] file.dsp ...\n", argv[0]);
        return 2;
    }

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    /* A file of its own in the system's temporary directory: two runs at
       once -- parallel ctest, two build trees -- writing one fixed name
       edit each other's copies. */
    const string tmp = thUtil::tempFile("dspwrite-");

    if (tmp.empty())
    {
        fprintf(stderr, "dspwrite: no temporary file to work in\n");
        return 1;
    }

    int failed = 0, files = 0, edits = 0, skipped = 0, inserted = 0;

    /* The header edits the corpus cannot show: every shipped file has all
       four fields, in one order. */
    {
        const string body =
            "node ionode { channels = 1; out0 = o->out; play = 1; };\n"
            "node o osc::simple { freq = ionode->note; };\nio ionode;\n";
        struct Case {
            const char *what, *before, *field, *value;
            NodeEdit::Result result;
            const char *after;
        };
        const Case cases[] = {
            { "a header put in a file with none, after its comments",
              "# A patch\n\n", "name", "Bare", NodeEdit::OK,
              "# A patch\n\nname \"Bare\";\n\n" },
            { "a field put in after the one before it",
              "name \"X\";\ndescription \"D\";\n", "author", "A",
              NodeEdit::OK,
              "name \"X\";\nauthor \"A\";\ndescription \"D\";\n" },
            { "a field put in before the one after it",
              "author \"A\";\n", "name", "X", NodeEdit::OK,
              "name \"X\";\nauthor \"A\";\n" },
            { "a quote refused, the file untouched",
              "name \"X\";\n", "author", "say \"hi\"", NodeEdit::UNWRITABLE,
              "name \"X\";\n" },
            { "a field the format has not, refused",
              "name \"X\";\n", "license", "GPL", NodeEdit::REFUSED,
              "name \"X\";\n" },
            { "a value with no space before its quote, found",
              "name \"X\";\nauthor\"A\";\n", "author", "B", NodeEdit::OK,
              "name \"X\";\nauthor\"B\";\n" },
            { "a value on the line after its keyword, found",
              "name \"X\";\nauthor\n    \"A\";\n", "author", "B",
              NodeEdit::OK, "name \"X\";\nauthor\n    \"B\";\n" },
            { "a field taken out of a line it shares, the other left",
              "name \"X\"; author \"A\"; category \"Keys\";\n", "author",
              "", NodeEdit::OK, "name \"X\"; category \"Keys\";\n" },
            { "a field set on a line it shares",
              "name \"X\"; author \"A\"; category \"Keys\";\n",
              "category", "Bass", NodeEdit::OK,
              "name \"X\"; author \"A\"; category \"Bass\";\n" },
            { "a field put in a CRLF file ends its line as the file does",
              "name \"X\";\r\ndescription \"D\";\r\n", "author", "A",
              NodeEdit::OK,
              "name \"X\";\r\nauthor \"A\";\r\ndescription \"D\";\r\n" },
            { "a field taken out of a CRLF file takes its whole line",
              "name \"X\";\r\nauthor \"A\";\r\n", "author", "",
              NodeEdit::OK, "name \"X\";\r\n" },
            { "an author inside a node's braces is not the header's",
              "node q misc::note { author \"Z\"; };\n", "author", "A",
              NodeEdit::OK,
              "author \"A\";\n\nnode q misc::note { author \"Z\"; };\n" },
        };

        /* The header last, with a comment and no line ending after it. */
        {
            string text = body + "name \"X\"; # the header", why;
            const NodeEdit::Result r =
                NodeEdit::Text::setInfo(text, "author", "A", why);

            if (r != NodeEdit::OK ||
                text != body + "name \"X\"; # the header\nauthor \"A\";")
            {
                printf("FAIL  setInfo: a field put in after a last line "
                       "with no line ending -> %s\n%s",
                       NodeEdit::resultText(r), text.c_str());
                failed++;
            }
            else if (!quiet)
                printf("ok    setInfo: a field put in after a last line "
                       "with no line ending\n");
        }

        for (const Case &c : cases)
        {
            string text = string(c.before) + body, why;
            const NodeEdit::Result r =
                NodeEdit::Text::setInfo(text, c.field, c.value, why);

            if (r != c.result || text != string(c.after) + body)
            {
                printf("FAIL  setInfo: %s -> %s (%s)\n%s", c.what,
                       NodeEdit::resultText(r), why.c_str(), text.c_str());
                failed++;
            }
            else if (!quiet)
                printf("ok    setInfo: %s\n", c.what);
        }
    }
    /* Renames, as the editor makes them: the patch, then its layout. */
    {
        struct Case {
            const char *what;
            bool control;
            const char *from, *to, *before;
            NodeEdit::Result result;
            const char *after;
        };
        const Case cases[] = {
            { "a node's block, its readers, its layout and its probe",
              false, "osc", "lfo",
              "node osc osc::simple { };\n"
              "node f filt::svf { in = osc->out; };\n"
              "# @layout osc 1 2\n# @layout oscx 3 4\n"
              "# @probe osc out meter\n",
              NodeEdit::OK,
              "node lfo osc::simple { };\n"
              "node f filt::svf { in = lfo->out; };\n"
              "# @layout lfo 1 2\n# @layout oscx 3 4\n"
              "# @probe lfo out meter\n" },
            { "an arg and a port of the same name left alone",
              false, "osc", "lfo",
              "node osc a::b { osc = osc->osc; };\n", NodeEdit::OK,
              "node lfo a::b { osc = lfo->osc; };\n" },
            { "a reader in arithmetic over two lines",
              false, "osc", "lfo",
              "node osc a::b { };\nnode f a::b { in = 2 *\n    osc->out; };\n",
              NodeEdit::OK,
              "node lfo a::b { };\nnode f a::b { in = 2 *\n    lfo->out; };\n" },
            { "comments, strings and a control of the same name left alone",
              false, "osc", "lfo",
              "# osc->out\nname \"osc\";\n@osc = 1;\n"
              "node osc a::b { in = @osc; };\n",
              NodeEdit::OK,
              "# osc->out\nname \"osc\";\n@osc = 1;\n"
              "node lfo a::b { in = @osc; };\n" },
            { "a name another starts with left alone",
              false, "os", "q",
              "node os a::b { };\nnode myos a::b { };\n"
              "node osc a::b { in = os->out + myos->out; };\n",
              NodeEdit::OK,
              "node q a::b { };\nnode myos a::b { };\n"
              "node osc a::b { in = q->out + myos->out; };\n" },
            { "an array and every element read",
              false, "tone", "voice",
              "node tone[2] a::b { };\n"
              "node t a::b { in0 = tone[1]->out; in1 = tone[]->out; };\n",
              NodeEdit::OK,
              "node voice[2] a::b { };\n"
              "node t a::b { in0 = voice[1]->out; in1 = voice[]->out; };\n" },
            { "one element of an array refused",
              false, "tone[1]", "voice", "node tone[2] a::b { };\n",
              NodeEdit::REFUSED, "node tone[2] a::b { };\n" },
            { "the io node, its line and both its halves",
              false, "ionode", "main",
              "node ionode { out0 = ionode->in0; };\nio ionode;\n"
              "# @layout ionode#in 0 0\n# @layout ionode#out 5 0\n",
              NodeEdit::OK,
              "node main { out0 = main->in0; };\nio main;\n"
              "# @layout main#in 0 0\n# @layout main#out 5 0\n" },
            { "a name another node has refused",
              false, "a", "b", "node a x::y { };\nnode b[2] x::y { };\n",
              NodeEdit::REFUSED, "node a x::y { };\nnode b[2] x::y { };\n" },
            { "a name the lexer will not read refused",
              false, "a", "2a", "node a x::y { };\n",
              NodeEdit::REFUSED, "node a x::y { };\n" },
            { "a node that is not there",
              false, "z", "y", "node a x::y { };\n",
              NodeEdit::NO_NODE, "node a x::y { };\n" },
            { "a node renamed to its own name changes nothing",
              false, "a", "a", "node a x::y { };\n",
              NodeEdit::OK, "node a x::y { };\n" },
            { "a control's block, its readers and its layout",
              true, "a", "cut",
              "@a = 0.5;\n@a.min = 0;\n@ab = 1;\n"
              "node n x::y { in = @a + @ab; a = a->out; };\n"
              "# @layout @a 3 4\n# @layout a 5 6\n",
              NodeEdit::OK,
              "@cut = 0.5;\n@cut.min = 0;\n@ab = 1;\n"
              "node n x::y { in = @cut + @ab; a = a->out; };\n"
              "# @layout @cut 3 4\n# @layout a 5 6\n" },
            { "a name another control has refused",
              true, "a", "ab", "@a = 0.5;\n@ab = 1;\n",
              NodeEdit::REFUSED, "@a = 0.5;\n@ab = 1;\n" },
            { "a control that is not there",
              true, "z", "y", "@a = 0.5;\n",
              NodeEdit::NO_NODE, "@a = 0.5;\n" },
            { "a controller's binding of a control, and no other string",
              true, "bright", "shine",
              "@bright = 1;\nnode n x::y { file = \"bright\"; };\n"
              "controller box {\n    base = \"bright\";\n"
              "    roll = \"bright\" { smooth = 0.05; };\n"
              "    tilt = \"brights\";\n};\n",
              NodeEdit::OK,
              "@shine = 1;\nnode n x::y { file = \"bright\"; };\n"
              "controller box {\n    base = \"bright\";\n"
              "    roll = \"shine\" { smooth = 0.05; };\n"
              "    tilt = \"brights\";\n};\n" },
            { "a name the lexer reads as a stray character refused",
              false, "a", "_a", "node a x::y { };\n",
              NodeEdit::REFUSED, "node a x::y { };\n" },
            { "a node named for a keyword refused",
              false, "a", "nil", "node a x::y { };\n",
              NodeEdit::REFUSED, "node a x::y { };\n" },
            { "a control named for a keyword refused",
              true, "a", "nil", "@a = 0.5;\n",
              NodeEdit::REFUSED, "@a = 0.5;\n" },
        };

        for (const Case &c : cases)
        {
            string text = c.before, why;
            const NodeEdit::Result r =
                c.control
                    ? NodeEdit::Text::renameControl(text, c.from, c.to, why)
                    : NodeEdit::Text::renameNode(text, c.from, c.to, why);

            if (r == NodeEdit::OK)
                NodeLayout::Text::rename(text,
                                         (c.control ? "@" : "") +
                                             string(c.from),
                                         (c.control ? "@" : "") +
                                             string(c.to));

            if (r != c.result || text != c.after)
            {
                printf("FAIL  rename: %s -> %s (%s)\n%s", c.what,
                       NodeEdit::resultText(r), why.c_str(), text.c_str());
                failed++;
            }
            else if (!quiet)
                printf("ok    rename: %s\n", c.what);
        }
    }

    /* An arg's arithmetic replaced with typed text. */
    {
        struct Case {
            const char *what, *node, *arg, *text, *before;
            NodeEdit::Result result;
            const char *after;
        };
        const Case cases[] = {
            { "arithmetic replaced where it stands",
              "o", "amp", "@lvl * 0.25",
              "node o a::b { freq = 2; amp = @lvl * 0.5;  # soft\n};\n",
              NodeEdit::OK,
              "node o a::b { freq = 2; amp = @lvl * 0.25;  # soft\n};\n" },
            { "arithmetic over three lines joined onto the last, its "
              "comments kept above",
              "o", "freq", "ionode->note * 2",
              "node o a::b {\n    freq = ionode->note  # the note\n"
              "        * exp2(@cents\n        / 1200);  # cents\n};\n",
              NodeEdit::OK,
              "node o a::b {\n    # the note\n"
              "    freq = ionode->note * 2;  # cents\n};\n" },
            { "the same arithmetic changes nothing",
              "o", "amp", "@lvl * 0.5", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::OK, "node o a::b { amp = @lvl * 0.5; };\n" },
            { "typed text that is a wire, taken",
              "o", "amp", "lfo->out", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::OK, "node o a::b { amp = lfo->out; };\n" },
            { "a number refused: its own row writes it",
              "o", "amp", "@lvl * 2", "node o a::b { amp = 0.5; };\n",
              NodeEdit::REFUSED, "node o a::b { amp = 0.5; };\n" },
            { "a wire refused: drawing one writes it",
              "o", "amp", "@lvl * 2", "node o a::b { amp = lfo->out; };\n",
              NodeEdit::REFUSED, "node o a::b { amp = lfo->out; };\n" },
            { "an arg that is not there",
              "o", "mul", "@lvl * 2", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::NO_NODE, "node o a::b { amp = @lvl * 0.5; };\n" },
            { "an array's block refused",
              "t[1]", "amp", "@lvl * 2",
              "node t[2] a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::REFUSED, "node t[2] a::b { amp = @lvl * 0.5; };\n" },
            { "nothing typed refused",
              "o", "amp", "  ", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::REFUSED, "node o a::b { amp = @lvl * 0.5; };\n" },
            { "a second statement refused",
              "o", "amp", "1; freq = 2", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::UNWRITABLE, "node o a::b { amp = @lvl * 0.5; };\n" },
            { "a comment refused, which would take the `;' with it",
              "o", "amp", "@lvl # half", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::UNWRITABLE, "node o a::b { amp = @lvl * 0.5; };\n" },
            { "a brace refused",
              "o", "amp", "@lvl }", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::UNWRITABLE, "node o a::b { amp = @lvl * 0.5; };\n" },
            { "a string refused",
              "o", "amp", "\"x\" * 2", "node o a::b { amp = @lvl * 0.5; };\n",
              NodeEdit::UNWRITABLE, "node o a::b { amp = @lvl * 0.5; };\n" },
            { "a name in quotes refused: it is not arithmetic",
              "o", "file", "@x * 2", "node o a::b { file = \"k.wav\"; };\n",
              NodeEdit::REFUSED, "node o a::b { file = \"k.wav\"; };\n" },
            { "a line the io node writes per channel refused",
              "ionode", "out0", "m0->out",
              "node ionode { channels = 2; out[] = m[]->out * 0.5; };\n",
              NodeEdit::REFUSED,
              "node ionode { channels = 2; out[] = m[]->out * 0.5; };\n" },
        };

        for (const Case &c : cases)
        {
            string text = c.before, why;
            const NodeEdit::Result r =
                NodeEdit::Text::setExpression(text, c.node, c.arg, c.text, why);

            if (r != c.result || text != c.after)
            {
                printf("FAIL  setExpression: %s -> %s (%s)\n%s", c.what,
                       NodeEdit::resultText(r), why.c_str(), text.c_str());
                failed++;
            }
            else if (!quiet)
                printf("ok    setExpression: %s\n", c.what);
        }
    }

    int unwritable = 0, noops = 0, respelt = 0;
    int wiresCut = 0, wireNoops = 0;
    int controlsMoved = 0, controlNoops = 0, controlsRespelt = 0;
    int metaNoops = 0, metaChanged = 0, metaRestored = 0, metaClamped = 0;
    int removals = 0, infos = 0, renames = 0, expressions = 0;

    for (int f = firstFile; f < argc; f++)
    {
        thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

        thSynthTree *tree = synth.parseTree(argv[f]);

        if (tree == NULL) { skipped++; continue; }

        NodeGraph g;

        g.build(tree);
        delete tree;

        files++;

        string original;

        if (!slurp(argv[f], original))
        { printf("FAIL  %s: unreadable\n", argv[f]); failed++; continue; }

        int problems = 0;

        for (size_t b = 0; b < g.boxes().size() && problems < 4; b++)
        {
            const NodeGraph::Box &bx = g.boxes()[b];

            /* The io source half has no node behind it. */
            if (bx.isIoSource)
                continue;

            for (size_t k = 0; k < bx.params.size() && problems < 4; k++)
            {
                const NodeGraph::Param &p = bx.params[k];

                if (NodeEdit::Text::shared(original, bx.name, p.name))
                    continue;

                if (p.kind != NodeGraph::Param::VALUE || p.isOutput)
                    continue;

                if (!spit(tmp, original))
                { printf("FAIL  %s: could not stage a copy\n", argv[f]);
                  problems++; break; }

                /* Something distinct from the current value, and positive so
                   it stays inside what the grammar can spell without relying
                   on the unary-minus rule. */
                const double target = (p.value == 0.0) ? 0.375
                                                       : fabs(p.value) * 1.5 + 0.125;

                string why;

                const bool wasPresent = (both::find(tmp, bx.name, p.name)
                                         == NodeEdit::OK);

                NodeEdit::Result r =
                    both::setValue(tmp, bx.name, p.name, target, why);

                if (r == NodeEdit::UNWRITABLE)
                { unwritable++; continue; }

                if (r != NodeEdit::OK)
                { printf("FAIL  %s: setValue(%s.%s) -> %s (%s)\n", argv[f],
                         bx.name.c_str(), p.name.c_str(),
                         NodeEdit::resultText(r), why.c_str());
                  problems++; continue; }

                edits++;

                /* 1. the parser must see exactly what we meant */
                float got = 0;

                if (!readValue(synth, tmp, bx.name, p.name, got))
                { printf("FAIL  %s: %s.%s unreadable after writing\n", argv[f],
                         bx.name.c_str(), p.name.c_str());
                  problems++; continue; }

                /* A float round-trip through nine decimal places is not exact;
                   what matters is that it is not a *different* number, e.g.
                   `5 ms' folded to 220.5 and written back as 220.5 raw. */
                const double tol = fabs(target) * 1e-5 + 1e-6;

                if (fabs((double)got - target) > tol)
                { printf("FAIL  %s: %s.%s written as %g, parsed back as %g\n",
                         argv[f], bx.name.c_str(), p.name.c_str(), target,
                         (double)got);
                  problems++; continue; }

                /* 2. exactly one line differs -- or one more line, if the arg
                      was not in the file to begin with */
                string after;

                if (!slurp(tmp, after))
                { printf("FAIL  %s: could not re-read\n", argv[f]);
                  problems++; continue; }

                const int nd = changedLines(original, after);

                if (nd != 1)
                { printf("FAIL  %s: editing %s.%s changed %d lines\n", argv[f],
                         bx.name.c_str(), p.name.c_str(), nd);
                  problems++; continue; }

                if (after.size() > original.size() &&
                    after.find("\n" ) != string::npos &&
                    changedLines(original, after) == 1 &&
                    after.length() != original.length())
                {
                    /* an inserted line is fine; just counted for the report */
                }

                /* 3. a write that changes nothing must change nothing. */
                {
                    string beforeNoop;

                    slurp(tmp, beforeNoop);

                    float cur = 0;

                    if (readValue(synth, tmp, bx.name, p.name, cur))
                    {
                        string w2;

                        both::setValue(tmp, bx.name, p.name, cur, w2);

                        string afterNoop;

                        slurp(tmp, afterNoop);

                        if (afterNoop != beforeNoop)
                        { printf("FAIL  %s: a no-op write to %s.%s changed the "
                                 "file\n", argv[f], bx.name.c_str(),
                                 p.name.c_str());
                          showFirstDiff(beforeNoop, afterNoop);
                          problems++; continue; }

                        noops++;
                    }
                }

                /* 4. putting the original value back must give the original
                      value back, in one line */
                r = both::setValue(tmp, bx.name, p.name, p.value, why);

                if (!wasPresent) inserted++;

                if (r != NodeEdit::OK)
                { printf("FAIL  %s: could not restore %s.%s (%s)\n", argv[f],
                         bx.name.c_str(), p.name.c_str(), why.c_str());
                  problems++; continue; }

                string restored;

                slurp(tmp, restored);

                if (restored != original)
                {
                    /* An arg the file never mentioned cannot come back
                       identical -- the line we inserted is still there, and
                       that is correct. What must hold is that nothing else
                       moved, and that the value reads back as it started. */
                    const int nd2 = changedLines(original, restored);

                    if (nd2 > 1)
                    { printf("FAIL  %s: restoring %s.%s changed %d lines\n",
                             argv[f], bx.name.c_str(), p.name.c_str(), nd2);
                      showFirstDiff(original, restored);
                      problems++; continue; }

                    if (wasPresent)
                        respelt++;
                }

                float back = 0;

                if (!readValue(synth, tmp, bx.name, p.name, back) ||
                    fabs((double)back - (double)p.value) >
                        fabs((double)p.value) * 1e-5 + 1e-6)
                { printf("FAIL  %s: %s.%s restored to %g, not %g\n", argv[f],
                         bx.name.c_str(), p.name.c_str(), (double)back,
                         (double)p.value);
                  problems++; }
            }
        }

        /* ---- controls ---- */

        for (size_t b = 0; b < g.boxes().size() && problems < 4; b++)
        {
            const NodeGraph::Box &bx = g.boxes()[b];

            if (!bx.isControl)
                continue;

            if (!spit(tmp, original))
            { printf("FAIL  %s: could not stage a copy\n", argv[f]);
              problems++; break; }

            /* Somewhere inside the declared range, and not where it already
               is -- a third of the way along, or two thirds if that happens
               to be the current value. */
            double target = bx.ctlMin + (bx.ctlMax - bx.ctlMin) / 3.0;

            if ((float)target == bx.ctlValue)
                target = bx.ctlMin + (bx.ctlMax - bx.ctlMin) * 2.0 / 3.0;

            string why;

            NodeEdit::Result r =
                both::setChanArg(tmp, bx.ctlArg, target, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: setChanArg(@%s) -> %s (%s)\n", argv[f],
                     bx.ctlArg.c_str(), NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            /* 8. the parser must see it, and the graph must show it on the
                  control box */
            {
                thSynthTree *t2 = synth.parseTree(tmp);

                if (t2 == NULL)
                { printf("FAIL  %s: will not parse after setting @%s\n",
                         argv[f], bx.ctlArg.c_str());
                  problems++; continue; }

                NodeGraph g2;

                g2.build(t2);
                delete t2;

                double got = 0;
                bool found = false;

                for (size_t q = 0; q < g2.boxes().size(); q++)
                    if (g2.boxes()[q].isControl &&
                        g2.boxes()[q].ctlArg == bx.ctlArg)
                    { got = g2.boxes()[q].ctlValue; found = true; break; }

                if (!found)
                { printf("FAIL  %s: @%s is no longer a control after writing\n",
                         argv[f], bx.ctlArg.c_str());
                  problems++; continue; }

                if (fabs(got - target) > fabs(target) * 1e-5 + 1e-6)
                { printf("FAIL  %s: @%s written as %g, read back as %g\n",
                         argv[f], bx.ctlArg.c_str(), target, got);
                  problems++; continue; }
            }

            controlsMoved++;

            /* 9. and back.
             *
             * Not byte-identical, necessarily: `@shape2 = 3.0' comes back as
             * `@shape2 = 3' once it has genuinely been moved, the same way
             * `th_max' comes back as `1'. The writer remembers when it need
             * not touch a line, not how a number used to be spelled. What
             * must hold is that nothing else moved. */
            r = both::setChanArg(tmp, bx.ctlArg, bx.ctlValue, why);

            string back;

            slurp(tmp, back);

            if (r != NodeEdit::OK || changedLines(original, back) > 1)
            { printf("FAIL  %s: restoring @%s changed %d lines\n", argv[f],
                     bx.ctlArg.c_str(), changedLines(original, back));
              showFirstDiff(original, back);
              problems++; continue; }

            if (back != original)
                controlsRespelt++;

            /* The strong one: writing the value that is already there must
               change nothing. Checked against whatever the file says now,
               not against the original. */
            const string beforeNoop = back;

            both::setChanArg(tmp, bx.ctlArg, bx.ctlValue, why);
            slurp(tmp, back);

            if (back != beforeNoop)
            { printf("FAIL  %s: a no-op write to @%s changed the file\n",
                     argv[f], bx.ctlArg.c_str());
              showFirstDiff(beforeNoop, back);
              problems++; continue; }

            controlNoops++;
        }

        /* ---- control metadata: .min, .max, .label, .group ---- */

        for (size_t b = 0; b < g.boxes().size() && problems < 4; b++)
        {
            const NodeGraph::Box &bx = g.boxes()[b];

            if (!bx.isControl)
                continue;

            /* From the tree rather than from the Box: Box::ctlLabel falls back
               to the control's name when the .dsp gives no .label, so it cannot
               say whether the line is there -- and writing a label the file
               never had is not a no-op. */
            string label0, group0;

            {
                thSynthTree *t0 = synth.parseTree(argv[f]);

                if (t0 == NULL)
                { printf("FAIL  %s: will not reparse\n", argv[f]);
                  problems++; break; }

                const thArg *ca = t0->getChanArg(bx.ctlArg);

                if (ca) { label0 = ca->label(); group0 = ca->group(); }

                delete t0;
            }

            if (!spit(tmp, original))
            { printf("FAIL  %s: could not stage a copy\n", argv[f]);
              problems++; break; }

            string why;

            /* 10. the strong one, and the same one (3) makes for values:
                   writing the metadata already there changes no byte. This is
                   what an editor that opens a file and saves it does. */
            NodeEdit::Result r =
                both::setControlMeta(tmp, bx.ctlArg, bx.ctlMin, bx.ctlMax,
                                         label0, group0, why);

            {
                string back;

                slurp(tmp, back);

                if (r != NodeEdit::OK)
                { printf("FAIL  %s: setControlMeta(@%s) -> %s (%s)\n", argv[f],
                         bx.ctlArg.c_str(), NodeEdit::resultText(r),
                         why.c_str());
                  problems++; continue; }

                if (back != original)
                { printf("FAIL  %s: a no-op metadata write to @%s changed the "
                         "file\n", argv[f], bx.ctlArg.c_str());
                  showFirstDiff(original, back);
                  problems++; continue; }
            }

            metaNoops++;

            /* 11. a real change: widen the range, and give it a label and a
                   group whether or not it had them.
             *
             * Strictly *containing* the old range, deliberately. Narrowing is
             * covered separately below, because it can move the value -- and a
             * clamp is one-way, so a test that narrows and then expects the
             * file back byte for byte is asserting something untrue. Eight
             * controls in the corpus have a negative minimum, and an earlier
             * version of this that clamped `bx.ctlMin - 1' at zero narrowed
             * exactly those and blamed the writer for it. */
            const double newMin = bx.ctlMin - 1.0;
            const double newMax = bx.ctlMax + 7.5;

            const string newLabel = "Test Label";
            const string newGroup = "Test Group";

            r = both::setControlMeta(tmp, bx.ctlArg, newMin, newMax,
                                         newLabel, newGroup, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: setControlMeta(@%s) -> %s (%s)\n", argv[f],
                     bx.ctlArg.c_str(), NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            {
                thSynthTree *t2 = synth.parseTree(tmp);

                if (t2 == NULL)
                { printf("FAIL  %s: will not parse after retyping @%s\n",
                         argv[f], bx.ctlArg.c_str());
                  problems++; continue; }

                thArg *ca = t2->getChanArg(bx.ctlArg);

                if (ca == NULL)
                { printf("FAIL  %s: @%s is no longer a chanarg after "
                         "retyping\n", argv[f], bx.ctlArg.c_str());
                  delete t2; problems++; continue; }

                const double gotMin = ca->min(), gotMax = ca->max();
                const string gotLabel = ca->label(), gotGroup = ca->group();
                const double gotValue = (ca->len() > 0) ? (*ca)[0] : 0;

                delete t2;

                const double tol = fabs(newMax) * 1e-5 + 1e-6;

                if (fabs(gotMin - newMin) > tol || fabs(gotMax - newMax) > tol)
                { printf("FAIL  %s: @%s range written as %g..%g, read back as "
                         "%g..%g\n", argv[f], bx.ctlArg.c_str(), newMin, newMax,
                         gotMin, gotMax);
                  problems++; continue; }

                if (gotLabel != newLabel || gotGroup != newGroup)
                { printf("FAIL  %s: @%s label/group written as \"%s\"/\"%s\", "
                         "read back as \"%s\"/\"%s\"\n", argv[f],
                         bx.ctlArg.c_str(), newLabel.c_str(), newGroup.c_str(),
                         gotLabel.c_str(), gotGroup.c_str());
                  problems++; continue; }

                /* The clamp: the value must always be inside the range the
                   file declares, whatever the range was moved to. */
                if (gotValue < gotMin - tol || gotValue > gotMax + tol)
                { printf("FAIL  %s: @%s = %g is outside its own %g..%g\n",
                         argv[f], bx.ctlArg.c_str(), gotValue, gotMin, gotMax);
                  problems++; continue; }
            }

            metaChanged++;

            /* 12. and back. Byte-identical this time, unlike the value case:
                   every line involved either existed and was rewritten in
                   place, or did not and is deleted again. A label the file
                   never had must leave no trace of having been added. */
            r = both::setControlMeta(tmp, bx.ctlArg, bx.ctlMin, bx.ctlMax,
                                         label0, group0, why);

            {
                string back;

                slurp(tmp, back);

                if (r != NodeEdit::OK || back != original)
                { printf("FAIL  %s: restoring the metadata of @%s did not "
                         "restore the file\n", argv[f], bx.ctlArg.c_str());
                  showFirstDiff(original, back);
                  problems++; continue; }
            }

            metaRestored++;

            /* 13. narrowing to a range the value is not in must move the
                   value, not leave a slider that cannot reach what the file
                   says. Above the value on purpose, so the expected answer is
                   the new minimum and not a coincidence. */
            {
                const double lo = (double)bx.ctlValue + 1.0;
                const double hi = lo + 1.0;

                r = both::setControlMeta(tmp, bx.ctlArg, lo, hi, label0,
                                             group0, why);

                if (r != NodeEdit::OK)
                { printf("FAIL  %s: narrowing @%s -> %s (%s)\n", argv[f],
                         bx.ctlArg.c_str(), NodeEdit::resultText(r),
                         why.c_str());
                  problems++; continue; }

                thSynthTree *t3 = synth.parseTree(tmp);

                if (t3 == NULL)
                { printf("FAIL  %s: will not parse after narrowing @%s\n",
                         argv[f], bx.ctlArg.c_str());
                  problems++; continue; }

                thArg *ca = t3->getChanArg(bx.ctlArg);

                const double got = (ca && ca->len() > 0) ? (*ca)[0] : 0;

                delete t3;

                if (fabs(got - lo) > fabs(lo) * 1e-5 + 1e-6)
                { printf("FAIL  %s: @%s was %g and the range moved to %g..%g, "
                         "but the value is %g\n", argv[f], bx.ctlArg.c_str(),
                         (double)bx.ctlValue, lo, hi, got);
                  problems++; continue; }

                metaClamped++;
            }
        }

        /* ---- wires ---- */

        for (size_t e = 0; e < g.edges().size() && problems < 4; e++)
        {
            const NodeGraph::Edge &ed = g.edges()[e];

            const NodeGraph::Box &tb = g.boxes()[ed.toBox];
            const NodeGraph::Box &fb = g.boxes()[ed.fromBox];

            /* A wire into or out of an expression box is not a line in the
               file. Its source is the arithmetic and its destination is a
               leaf of the arithmetic; neither end is a `node' block, and
               cutting one means removing the whole expression rather than
               restoring it afterwards. NodeGraph::canConnect refuses these
               for the same reason. */
            if (tb.isExpr || fb.isExpr)
                continue;

            const string arg = tb.ports[ed.toPort].name;
            const string port = fb.ports[ed.fromPort].name;

            if (NodeEdit::Text::shared(original, tb.name, arg))
                continue;

            if (!spit(tmp, original))
            { printf("FAIL  %s: could not stage a copy\n", argv[f]);
              problems++; break; }

            string why;

            /* 5. disconnecting must actually disconnect */
            NodeEdit::Result r =
                both::disconnect(tmp, tb.name, arg, 0, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: disconnect(%s.%s) -> %s (%s)\n", argv[f],
                     tb.name.c_str(), arg.c_str(),
                     NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            {
                thSynthTree *t2 = synth.parseTree(tmp);

                if (t2 == NULL)
                { printf("FAIL  %s: will not parse after disconnecting %s.%s\n",
                         argv[f], tb.name.c_str(), arg.c_str());
                  problems++; continue; }

                thNode *n2 = t2->findNode(tb.name);
                thArg *a2 = n2 ? n2->getArg(arg) : NULL;

                const bool stillWired =
                    (a2 && a2->type() == thArg::ARG_POINTER);

                delete t2;

                if (stillWired)
                { printf("FAIL  %s: %s.%s still wired after disconnect\n",
                         argv[f], tb.name.c_str(), arg.c_str());
                  problems++; continue; }
            }

            wiresCut++;

            /* 6. reconnecting must give the file back exactly */
            r = fb.isControl
                    ? both::connectControl(tmp, tb.name, arg, fb.ctlArg, why)
                    : both::connect(tmp, tb.name, arg, fb.name, port, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: connect(%s.%s <- %s->%s) -> %s (%s)\n", argv[f],
                     tb.name.c_str(), arg.c_str(), fb.name.c_str(),
                     port.c_str(), NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            string back;

            slurp(tmp, back);

            if (back != original)
            { printf("FAIL  %s: reconnecting %s.%s did not give the file back\n",
                     argv[f], tb.name.c_str(), arg.c_str());
              showFirstDiff(original, back);
              problems++; continue; }

            /* 7. connecting to where it already goes must change nothing */
            r = fb.isControl
                    ? both::connectControl(tmp, tb.name, arg, fb.ctlArg, why)
                    : both::connect(tmp, tb.name, arg, fb.name, port, why);

            slurp(tmp, back);

            if (r != NodeEdit::OK || back != original)
            { printf("FAIL  %s: reconnecting %s.%s to where it already goes "
                     "changed the file\n", argv[f], tb.name.c_str(),
                     arg.c_str());
              showFirstDiff(original, back);
              problems++; continue; }

            wireNoops++;
        }

        /* 14. every node and every control removed, one at a time */
        for (size_t b = 0; b < g.boxes().size() && problems < 4; b++)
        {
            const NodeGraph::Box &bx = g.boxes()[b];

            /* The io node is not one a file can do without, and an element
               of an array is the array's block to remove. */
            if (bx.isIoSource || bx.isIoSink || bx.isExpr || bx.isProbe ||
                (!bx.isControl && bx.name.find('[') != string::npos))
                continue;

            string text = original, why;
            int refs = 0;
            const NodeEdit::Result r =
                bx.isControl
                    ? NodeEdit::Text::removeControl(text, bx.ctlArg, refs, why)
                    : NodeEdit::Text::removeNode(text, bx.name, refs, why);

            if (r != NodeEdit::OK)
            { printf("FAIL  %s: removing %s -> %s (%s)\n", argv[f],
                     bx.name.c_str(), NodeEdit::resultText(r), why.c_str());
              problems++; continue; }

            thSynthTree *left = synth.parseTreeText(argv[f], text);

            if (left == NULL)
            { printf("FAIL  %s: removing %s leaves a file that does not "
                     "parse\n", argv[f], bx.name.c_str());
              problems++; continue; }

            delete left;
            removals++;
        }

        /* 15. every header field changed, restored, removed and re-added */
        for (const char *field : { "name", "author", "description",
                                   "category" })
        {
            if (problems >= 4)
                break;

            const auto said = [&] (const string &text, string &out)
            {
                thSynthTree *t = synth.parseTreeText(argv[f], text);

                if (t == NULL)
                    return false;

                const string which = field;

                if (which == "name")
                    out = t->name();
                else if (which == "description")
                    out = t->desc();
                else if (which == "category")
                    out = t->category();
                else
                {
                    thArg *a = t->getChanArg("author");

                    out = a != NULL ? a->comment() : string();
                }

                delete t;
                return true;
            };

            string was, why;

            if (!said(original, was) || was.empty())
                continue;

            string text = original, now;
            NodeEdit::Result r =
                NodeEdit::Text::setInfo(text, field, "Changed " + was, why);

            if (r != NodeEdit::OK || !said(text, now) ||
                now != "Changed " + was)
            { printf("FAIL  %s: %s set -> %s (%s), reads back \"%s\"\n",
                     argv[f], field, NodeEdit::resultText(r), why.c_str(),
                     now.c_str());
              problems++; continue; }

            r = NodeEdit::Text::setInfo(text, field, was, why);

            if (r != NodeEdit::OK || text != original)
            { printf("FAIL  %s: %s put back is not the file\n", argv[f],
                     field);
              showFirstDiff(original, text);
              problems++; continue; }

            /* A name the file cannot do without. */
            if (string(field) == "name")
            {
                string bare = original;

                if (NodeEdit::Text::setInfo(bare, field, "", why) !=
                    NodeEdit::REFUSED || bare != original)
                { printf("FAIL  %s: an empty name was taken\n", argv[f]);
                  problems++; }

                infos++;
                continue;
            }

            if (NodeEdit::Text::setInfo(text, field, "", why) != NodeEdit::OK ||
                !said(text, now) || !now.empty() ||
                NodeEdit::Text::setInfo(text, field, was, why) != NodeEdit::OK ||
                text != original)
            { printf("FAIL  %s: %s taken out and put back is not the file\n",
                     argv[f], field);
              showFirstDiff(original, text);
              problems++; continue; }

            infos++;
        }

        /* 16. every node and every control renamed: the graph is the same
           graph under the new name, and renaming it back is the file. */
        {
            /* Every box and wire, by name, with `from' read as `to'. An
               expression box is `host.arg' and an element `name[i]'. */
            const auto shape = [&] (const string &text, const string &from,
                                    const string &to, vector<string> &out)
            {
                thSynthTree *t = synth.parseTreeText(argv[f], text);
                NodeGraph h;

                if (t == NULL)
                    return false;

                const bool built = h.build(t);

                delete t;

                if (!built)
                    return false;

                const auto as = [&] (const string &name)
                {
                    const char next = name.size() > from.size()
                                          ? name[from.size()] : 0;

                    if (name.compare(0, from.size(), from) == 0 &&
                        (next == 0 || next == '.' || next == '['))
                        return to + name.substr(from.size());

                    return name;
                };
                /* A control's port is its name, and an expression's leaf
                   is an `@name' or a `node->port'. */
                const auto port = [&] (int box, int at)
                {
                    const NodeGraph::Box &bx = h.boxes()[box];
                    string p = at >= 0 && at < (int)bx.ports.size()
                                   ? bx.ports[at].name : string();

                    if (bx.name == from && "@" + p == from)
                        return to.substr(1);

                    if (p == from && from[0] == '@')
                        return to;

                    if (p.compare(0, from.size() + 2, from + "->") == 0)
                        p = to + p.substr(from.size());

                    return p;
                };

                out.clear();

                for (const NodeGraph::Box &bx : h.boxes())
                    out.push_back(as(bx.name) + (bx.isIoSource ? "#in" : "") +
                                  (bx.isIoSink ? "#out" : ""));

                for (const NodeGraph::Edge &e : h.edges())
                    out.push_back(as(h.boxes()[e.fromBox].name) + "->" +
                                  port(e.fromBox, e.fromPort) + " => " +
                                  as(h.boxes()[e.toBox].name) + "." +
                                  port(e.toBox, e.toPort));

                std::sort(out.begin(), out.end());
                return true;
            };

            vector<string> names, controls;

            for (const NodeGraph::Box &bx : g.boxes())
                if (bx.isControl)
                    controls.push_back(bx.ctlArg);
                else if (!bx.isExpr && !bx.isProbe && !bx.isIoSink &&
                         bx.name.find('[') == string::npos)
                    names.push_back(bx.name);

            for (int kind = 0; kind < 2; kind++)
            {
                const bool control = kind == 1;
                const vector<string> &list = control ? controls : names;

                for (size_t i = 0; i < list.size() && problems < 4; i++)
                {
                    const string from = list[i], to = "renamed_" + from;
                    const string pf = (control ? "@" : "") + from;
                    const string pt = (control ? "@" : "") + to;
                    const auto rename = [&] (string &text, const string &a,
                                             const string &b, string &why)
                    {
                        return control
                                   ? NodeEdit::Text::renameControl(text, a, b,
                                                                   why)
                                   : NodeEdit::Text::renameNode(text, a, b,
                                                                why);
                    };

                    string text = original, why;
                    vector<string> want, got;
                    NodeEdit::Result r = rename(text, from, to, why);

                    if (r != NodeEdit::OK ||
                        !shape(original, pf, pt, want) ||
                        !shape(text, pf, pf, got) || got != want)
                    {
                        size_t k = 0;

                        while (k < want.size() && k < got.size() &&
                               want[k] == got[k])
                            k++;

                        printf("FAIL  %s: renaming %s -> %s (%s) is not the "
                               "same graph: wanted [%s], got [%s]\n", argv[f],
                               pf.c_str(), NodeEdit::resultText(r),
                               why.c_str(),
                               k < want.size() ? want[k].c_str() : "",
                               k < got.size() ? got[k].c_str() : "");
                        problems++;
                        continue;
                    }

                    r = rename(text, to, from, why);

                    if (r != NodeEdit::OK || text != original)
                    { printf("FAIL  %s: %s renamed and back is not the "
                             "file\n", argv[f], pf.c_str());
                      showFirstDiff(original, text);
                      problems++; continue; }

                    /* Onto a name the patch already has. */
                    if (list.size() > 1)
                    {
                        const string other = list[(i + 1) % list.size()];

                        if (rename(text, from, other, why) !=
                                NodeEdit::REFUSED || text != original)
                        { printf("FAIL  %s: %s renamed onto %s was taken\n",
                                 argv[f], pf.c_str(), other.c_str());
                          problems++; continue; }
                    }

                    renames++;
                }
            }

            /* 17. every expression set to the file's own text of it: the
               file, or on one line the same graph; and set to that times
               two, which reads back as written. */
            const string none = "@\x01";
            vector<string> want, got;

            for (size_t b = 0; b < g.boxes().size() && problems < 4; b++)
            {
                const NodeGraph::Box &bx = g.boxes()[b];
                string own, text = original, why;

                if (!bx.isExpr || !NodeEdit::Text::expression(
                                      original, bx.exprNode, bx.exprArg, own))
                    continue;

                NodeEdit::Result r = NodeEdit::Text::setExpression(
                    text, bx.exprNode, bx.exprArg, own, why);

                /* The shape has its wires, not its arithmetic. */
                const auto reads = [&] (const string &t, string &out)
                {
                    thSynthTree *tree = synth.parseTreeText(argv[f], t);
                    NodeGraph h;
                    const bool built = tree != NULL && h.build(tree);

                    delete tree;

                    for (size_t k = 0; built && k < h.boxes().size(); k++)
                        if (h.boxes()[k].isExpr && h.boxes()[k].name == bx.name)
                        {
                            out = h.boxes()[k].exprText;
                            return true;
                        }

                    return false;
                };
                string now;

                if (r != NodeEdit::OK ||
                    (text != original &&
                     (!shape(original, none, none, want) ||
                      !shape(text, none, none, got) || got != want ||
                      !reads(text, now) || now != bx.exprText)))
                { printf("FAIL  %s: %s set to its own \"%s\" -> %s (%s) is "
                         "neither the file nor the same graph\n", argv[f],
                         bx.name.c_str(), own.c_str(), NodeEdit::resultText(r),
                         why.c_str());
                  showFirstDiff(original, text);
                  problems++; continue; }

                const string twice = "(" + own + ") * 2";

                text = original;
                r = NodeEdit::Text::setExpression(text, bx.exprNode,
                                                  bx.exprArg, twice, why);

                if (r != NodeEdit::OK || !reads(text, now) ||
                    !NodeEdit::Text::expression(text, bx.exprNode, bx.exprArg,
                                                now) || now != twice)
                { printf("FAIL  %s: %s set to \"%s\" -> %s (%s) reads back "
                         "\"%s\"\n", argv[f], bx.name.c_str(), twice.c_str(),
                         NodeEdit::resultText(r), why.c_str(), now.c_str());
                  problems++; continue; }

                expressions++;
            }
        }

        if (problems)
            failed++;
        else if (!quiet)
            printf("ok    %-34s\n", argv[f]);
    }

    remove(tmp.c_str());

    if (both::disagreements > 0)
        failed += both::disagreements;

    printf("\n%d files, %d failed, %d skipped (would not load)\n",
           files, failed, skipped);
    printf("  %d values rewritten and restored, %d inserted, %d unwritable\n",
           edits, inserted, unwritable);
    printf("  %d no-op writes, every one byte-identical; %d came back correct "
           "but respelt\n", noops, respelt);
    printf("  %d wires cut and restored, %d reconnects to where they already "
           "went, all byte-identical\n", wiresCut, wireNoops);
    printf("  %d controls moved and restored (%d respelt), %d no-op writes, "
           "every one byte-identical\n", controlsMoved, controlsRespelt,
           controlNoops);
    printf("  %d control ranges retyped and restored byte-identically, "
           "%d no-op metadata writes, every one byte-identical\n",
           metaRestored, metaNoops);
    printf("  %d edits made twice, over a file and over its text; %d of "
           "them changed nothing and left the file untouched, and none "
           "left a temporary behind\n", both::checked, both::suppressed);
    printf("  %d values clamped by a range narrowed past them\n", metaClamped);
    printf("  %d nodes and controls removed, one at a time, each leaving a "
           "file that parses\n", removals);
    printf("  %d header fields changed, restored, and taken out and put "
           "back, each byte-identical\n", infos);
    printf("  %d nodes and controls renamed, each the same graph, and "
           "back, each byte-identical\n", renames);
    printf("  %d expressions set to their own text, each the file or the "
           "same graph, and to twice it\n", expressions);

    if (metaChanged != metaRestored)
        printf("  (%d retyped but not restored)\n", metaChanged - metaRestored);

    return exitCode(failed);
}
