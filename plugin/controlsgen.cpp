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
 * controlsgen -- the plugin's controls, read off the engine at build time.
 *
 *   controlsgen thinksynth_controls.h thinksynth_ui_size.h
 *
 * Loads the embedded graph (ReadControls.cpp) and writes what a host and
 * the editor need to know about it as plain data: the control table and
 * the graph's description into the first file, the editor's size into the
 * second -- which DistrhoPluginInfo.h includes for DPF's default UI size,
 * so the size a host is told before the editor exists is the panel's.
 *
 * Each file is written only if what it would say differs from what it
 * says, so a build that changed nothing recompiles nothing. Exit status
 * is 1 if the graph did not load or a file could not be written.
 */

#include <stdio.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Controls.h"
#include "Panel.h"

#include "thinksynth_dsp.h"

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

static bool writeIfChanged (const char *path, const std::string &text)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream old;

    old << in.rdbuf();

    if (in && old.str() == text)
        return true;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);

    out << text;

    return (bool)out;
}

int main (int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s CONTROLS.h UISIZE.h\n", argv[0]);
        return 1;
    }

    const std::vector<Control> c = readControls();

    if (c.empty())
    {
        fprintf(stderr, "controlsgen: %s did not load\n", thPluginDspName);
        return 1;
    }

    std::ostringstream t;

    t << "/* " << thPluginDspName << "'s controls, read off the engine by "
         "controlsgen. */\n\n"
      << "static const char kControlDescription[] = "
      << quoted(thPluginDspDescription) << ";\n\n"
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

    const Panel panel(c, "", "");
    std::ostringstream u;

    u << "/* The editor's size, in unscaled pixels, from its panel. Written "
         "by\n   controlsgen. */\n"
      << "#define DISTRHO_UI_DEFAULT_WIDTH  " << panel.width() << "\n"
      << "#define DISTRHO_UI_DEFAULT_HEIGHT " << panel.height() << "\n";

    if (!writeIfChanged(argv[1], t.str()) || !writeIfChanged(argv[2], u.str()))
    {
        fprintf(stderr, "controlsgen: could not write %s or %s\n", argv[1],
                argv[2]);
        return 1;
    }

    return 0;
}
