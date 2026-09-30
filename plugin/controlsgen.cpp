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
 *   controlsgen GRAPH.dsp ID UID TEMPLATE OUTDIR [SAMPLEDIR...]
 *
 * Loads the graph and writes into OUTDIR:
 *
 *   DistrhoPluginInfo.h    TEMPLATE (DistrhoPluginInfo.h.in) with the
 *                          plugin's identity filled in: ID, the four
 *                          characters UID, and the graph's own name and
 *                          category
 *   thinksynth_dsp.h       the graph's text, its name, the plugin's label
 *                          (thinksynth_ID) and the graph's description
 *   thinksynth_controls.h  the control table (ReadControls.cpp) as data
 *   thinksynth_ui_size.h   the editor's size, measured off its panel; what
 *                          DistrhoPluginInfo.h gives DPF as the default
 *   thinksynth_samples.cpp every wav the graph names, as bytes, and the
 *                          function that registers them with libthink
 *                          (thUtil::addEmbeddedFile) under samples/, where
 *                          osc::sample looks before it looks on disk
 *   generated.stamp        touched every run, which is what a build system
 *                          hangs the command on
 *
 * All of it at build time and none at configure time, so a graph whose
 * name changes is a plugin whose name changes on the next build.
 *
 * What the graph says is read with libthink's lexer, not by pattern: a
 * `#' inside a string is not a comment, and a pattern that thought so lost
 * the string. A wav is any quoted string ending in .wav, in any case and
 * in any folder under samples/ -- "sub/kick.wav" -- looked for in each
 * SAMPLEDIR in turn and registered under the name the graph wrote. One
 * that is absolute or climbs out with `..' cannot go in a plugin, and is
 * refused; so is one that is not found, and so is an effect graph -- one
 * that takes input -- rather than any of them being built into a plugin
 * that plays silence.
 *
 * Each file is written only if what it would say differs from what it
 * says, so a build that changed nothing recompiles nothing. Exit status
 * is 1 on any failure, with a line saying which.
 */

#include <stdio.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"
#include "thLexer.h"

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

/* `s' as it goes inside a C string literal's quotes. */
static std::string escaped (const std::string &s)
{
    const std::string q = quoted(s);

    return q.substr(1, q.size() - 2);
}

static bool endsWithWav (const std::string &s)
{
    if (s.size() < 5)
        return false;

    const std::string tail = s.substr(s.size() - 4);

    return tail[0] == '.' && (tail[1] == 'w' || tail[1] == 'W') &&
           (tail[2] == 'a' || tail[2] == 'A') &&
           (tail[3] == 'v' || tail[3] == 'V');
}

/* Every `key' in `text' replaced by `value'. */
static void fill (std::string &text, const std::string &key,
                  const std::string &value)
{
    for (size_t at = text.find(key); at != std::string::npos;
         at = text.find(key, at + value.size()))
        text.replace(at, key.size(), value);
}

int main (int argc, char **argv)
{
    if (argc < 6)
    {
        fprintf(stderr, "usage: %s GRAPH.dsp ID UID TEMPLATE OUTDIR "
                        "[SAMPLEDIR...]\n", argv[0]);
        return 1;
    }

    const std::string path = argv[1], id = argv[2], uid = argv[3];
    const std::string templatePath = argv[4], outdir = argv[5];
    const int firstSampleDir = 6;
    std::string text, info;

    if (uid.size() != 4)
        return fail("the unique id is four characters, and " + uid +
                    " is not");

    if (!slurp(templatePath, info))
        return fail("could not read " + templatePath);

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

    /* What the graph says about itself, and the wavs it names, off the
       lexer's tokens: `name "..."' and its kin are a word and a string. */
    std::vector<thLexToken> tokens;
    std::string graphName, description, category;
    std::vector<std::string> wavs;

    thLexString(text, tokens);

    for (size_t i = 0; i < tokens.size(); i++)
    {
        if (tokens[i].kind == thLexToken::WORD && i + 1 < tokens.size() &&
            tokens[i + 1].kind == thLexToken::STRING)
        {
            const std::string &word = tokens[i].text;
            const std::string &value = tokens[i + 1].text;

            if (word == "name" && graphName.empty())
                graphName = value;
            else if (word == "description" && description.empty())
                description = value;
            else if (word == "category" && category.empty())
                category = value;
        }

        if (tokens[i].kind != thLexToken::STRING ||
            !endsWithWav(tokens[i].text))
            continue;

        const std::string &wav = tokens[i].text;

        if (wav[0] == '/' || wav.find('\\') != std::string::npos ||
            wav.find(':') != std::string::npos ||
            wav.find("..") != std::string::npos)
            return fail(path + " names " + wav + ", which is not under "
                        "samples/ and cannot be compiled into a plugin");

        bool seen = false;

        for (size_t k = 0; k < wavs.size() && !seen; k++)
            seen = wavs[k] == wav;

        if (!seen)
            wavs.push_back(wav);
    }

    if (graphName.empty())
        graphName = id;

    /* DistrhoPluginInfo.h */
    const bool drum = category == "Drums";

    fill(info, "@THINK_PLUGIN_DSP@", name);
    fill(info, "@THINK_PLUGIN_ID@", id);
    fill(info, "@THINK_PLUGIN_NAME@", escaped("thinksynth " + graphName));
    fill(info, "@THINK_PLUGIN_UNIQUE_ID@", uid);
    fill(info, "@THINK_PLUGIN_UNIQUE_ID_CHARS@",
         std::string("'") + uid[0] + "', '" + uid[1] + "', '" + uid[2] +
         "', '" + uid[3] + "'");
    fill(info, "@THINK_PLUGIN_CLAP_KIND@",
         drum ? "\"drum\"" : "\"synthesizer\"");
    fill(info, "@THINK_PLUGIN_VST3_KIND@", drum ? "Drum" : "Synth");

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

    /* thinksynth_samples.cpp */
    std::ostringstream w;

    w << "/* The samples " << name << " plays, embedded. Written by "
         "controlsgen. */\n"
      << "#include <stddef.h>\n\n#include \"thUtil.h\"\n\n";

    for (size_t k = 0; k < wavs.size(); k++)
    {
        std::string bytes;
        bool found = false;

        for (int dir = firstSampleDir; dir < argc && !found; dir++)
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

    if (!writeIfChanged(outdir + "/DistrhoPluginInfo.h", info) ||
        !writeIfChanged(outdir + "/thinksynth_dsp.h", d.str()) ||
        !writeIfChanged(outdir + "/thinksynth_controls.h", t.str()) ||
        !writeIfChanged(outdir + "/thinksynth_ui_size.h", u.str()) ||
        !writeIfChanged(outdir + "/thinksynth_samples.cpp", w.str()))
        return fail("could not write into " + outdir);

    /* Always, so the command's output is newer than its inputs whether or
       not anything above changed. */
    std::ofstream stamp((outdir + "/generated.stamp").c_str(),
                        std::ios::trunc);

    stamp << "\n";

    return stamp ? 0 : fail("could not write into " + outdir);
}
