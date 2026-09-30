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
 * controlsgen -- what one .dsp's plugin compiles in, read off the engine
 * at build time.
 *
 *   controlsgen GRAPH.dsp ID OUTDIR [SAMPLEDIR...]
 *
 * Loads the graph and writes into OUTDIR:
 *
 *   thinksynth_dsp.h       the graph's text, its name, the plugin's label
 *                          (thinksynth_ID) and the graph's description
 *   thinksynth_controls.h  the control table (ReadControls.cpp) as data
 *   thinksynth_ui_size.h   the editor's size, measured off its panel; what
 *                          DistrhoPluginInfo.h gives DPF as the default
 *   thinksynth_samples.cpp every wav the graph names, as bytes, and the
 *                          function that registers them with libthink
 *                          (thUtil::addEmbeddedFile) under samples/, where
 *                          osc::sample looks before it looks on disk
 *
 * A wav is looked for in each SAMPLEDIR in turn. The graph has to be an
 * instrument: an effect graph -- one that takes input -- is refused, as
 * is a sample that is not found, rather than built into a plugin that
 * would play silence.
 *
 * Each file is written only if what it would say differs from what it
 * says, so a build that changed nothing recompiles nothing. Exit status
 * is 1 on any failure, with a line saying which.
 */

#include <stdio.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"

#include "Controls.h"
#include "Panel.h"

static bool slurp (const std::string &path, std::string &out)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::ostringstream text;

    text << in.rdbuf();
    out = text.str();

    return (bool)in;
}

/* A C string literal of `s'. */
static std::string quoted (const std::string &s)
{
    std::string out = "\"";

    for (size_t i = 0; i < s.size(); i++)
    {
        const unsigned char c = (unsigned char)s[i];

        if (c == '"' || c == '\\')
            out += std::string("\\") + (char)c;
        else if (c == '\n')
            out += "\\n";
        else if (c < 0x20)
        {
            char buf[8];

            snprintf(buf, sizeof(buf), "\\%03o", c);
            out += buf;
        }
        else
            out += (char)c;
    }

    return out + "\"";
}

/* A float that reads back as itself. */
static std::string exact (float v)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "(float)%.9g", (double)v);

    return buf;
}

static bool writeIfChanged (const std::string &path, const std::string &text)
{
    std::string old;

    if (slurp(path, old) && old == text)
        return true;

    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);

    out << text;

    return (bool)out;
}

static int fail (const std::string &why)
{
    fprintf(stderr, "controlsgen: %s\n", why.c_str());
    return 1;
}

/* The graph with its comments taken out, for looking in. */
static std::string uncommented (const std::string &text)
{
    return std::regex_replace(text, std::regex("#[^\n]*"), "");
}

int main (int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s GRAPH.dsp ID OUTDIR [SAMPLEDIR...]\n",
                argv[0]);
        return 1;
    }

    const std::string path = argv[1], id = argv[2], outdir = argv[3];
    std::string text;

    if (!slurp(path, text))
        return fail("could not read " + path);

    const std::string name = path.substr(path.find_last_of("/\\") + 1);

    if (text.find(")thinksynth\"") != std::string::npos)
        return fail(path + " contains )thinksynth\", the raw string's "
                    "closing delimiter");

    /* An instrument, not an effect. */
    {
        thSynth synth("", kWindow, kReadRate);
        thSynthTree *tree = synth.parseTree(path);
        const bool effect = tree != NULL && tree->takesInput();

        delete tree;

        if (tree == NULL)
            return fail(path + " does not load");

        if (effect)
            return fail(path + " is an effect graph -- it takes input -- "
                        "and only instruments are built as plugins");
    }

    const std::vector<Control> c = readControls(name, text);

    if (c.empty())
        return fail(path + " did not load");

    const std::string plain = uncommented(text);
    std::smatch m;
    std::string description;

    if (std::regex_search(plain, m,
                          std::regex("(^|\n)[ \t]*description[ \t]+\"([^\"\n]*)\"")))
        description = m[2];

    /* thinksynth_dsp.h */
    std::ostringstream d;

    d << "/* " << name << ", embedded. Written by controlsgen. */\n"
      << "static const char thPluginDspName[] = " << quoted(name) << ";\n"
      << "static const char thPluginDspLabel[] = "
      << quoted("thinksynth_" + id) << ";\n"
      << "static const char thPluginDspDescription[] = "
      << quoted(description) << ";\n"
      << "static const char thPluginDspText[] = R\"thinksynth(" << text
      << ")thinksynth\";\n";

    /* thinksynth_controls.h */
    std::ostringstream t;

    t << "/* " << name << "'s controls, read off the engine by "
         "controlsgen. */\n\n"
      << "static const char kControlDescription[] = " << quoted(description)
      << ";\n\n"
      << "static const ControlRow kControlTable[] = {\n";

    for (size_t i = 0; i < c.size(); i++)
    {
        std::string names;

        for (size_t k = 0; k < c[i].valueNames.size(); k++)
            names += (k ? "\n" : "") + c[i].valueNames[k];

        t << "    { " << quoted(c[i].name) << ", " << quoted(c[i].label)
          << ", " << quoted(c[i].group) << ", " << quoted(c[i].units) << ",\n"
          << "      " << exact(c[i].min) << ", " << exact(c[i].max) << ", "
          << exact(c[i].def) << ", " << exact(c[i].step) << ", "
          << quoted(names) << " },\n";
    }

    t << "};\n";

    /* thinksynth_ui_size.h */
    const Panel panel(c, "", "");
    std::ostringstream u;

    u << "/* The editor's size, in unscaled pixels, from its panel. Written "
         "by\n   controlsgen. */\n"
      << "#define DISTRHO_UI_DEFAULT_WIDTH  " << panel.width() << "\n"
      << "#define DISTRHO_UI_DEFAULT_HEIGHT " << panel.height() << "\n";

    /* thinksynth_samples.cpp: every quoted .wav the graph names. */
    std::ostringstream w;
    std::vector<std::string> wavs;
    const std::regex quotedWav("\"([^\"/\\\\]+\\.wav)\"");

    for (std::sregex_iterator i(plain.begin(), plain.end(), quotedWav), e;
         i != e; ++i)
    {
        const std::string wav = (*i)[1];
        bool seen = false;

        for (size_t k = 0; k < wavs.size() && !seen; k++)
            seen = wavs[k] == wav;

        if (!seen)
            wavs.push_back(wav);
    }

    w << "/* The samples " << name << " plays, embedded. Written by "
         "controlsgen. */\n"
      << "#include <stddef.h>\n\n#include \"thUtil.h\"\n\n";

    for (size_t k = 0; k < wavs.size(); k++)
    {
        std::string bytes;
        bool found = false;

        for (int dir = 4; dir < argc && !found; dir++)
            found = slurp(std::string(argv[dir]) + "/" + wavs[k], bytes);

        if (!found)
            return fail(path + " names " + wavs[k] + ", which is in none of "
                        "the sample directories");

        w << "static const unsigned char sample" << k << "[] = {";

        for (size_t b = 0; b < bytes.size(); b++)
        {
            if (b % 16 == 0)
                w << "\n   ";

            w << " " << (unsigned)(unsigned char)bytes[b] << ",";
        }

        w << "\n};\n\n";
    }

    w << "/* Before the first synth loads the graph. */\n"
      << "void thPluginRegisterSamples (void)\n{\n";

    for (size_t k = 0; k < wavs.size(); k++)
        w << "    thUtil::addEmbeddedFile(" << quoted("samples/" + wavs[k])
          << ", sample" << k << ", sizeof(sample" << k << "));\n";

    w << "}\n";

    if (!writeIfChanged(outdir + "/thinksynth_dsp.h", d.str()) ||
        !writeIfChanged(outdir + "/thinksynth_controls.h", t.str()) ||
        !writeIfChanged(outdir + "/thinksynth_ui_size.h", u.str()) ||
        !writeIfChanged(outdir + "/thinksynth_samples.cpp", w.str()))
        return fail("could not write into " + outdir);

    return 0;
}
