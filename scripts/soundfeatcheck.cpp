/*
 * Copyright (C) 2026 The thinksynth authors
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

/*
 * soundfeatcheck -- does thSoundFeat's distance order changes the way an
 * ear did?
 *
 *   soundfeatcheck PLUGINS DSPDIR
 *
 * Each case renders a stock patch and two changes of it, and the change a
 * listener heard as the larger must be the farther. The Juno's chorus
 * switched off was audible and its chorus rate 10% faster was not. A
 * distance that compares frames one instant at a time called the faster
 * chorus the farther, because a chorus a little off is a different chorus
 * at every instant and no chorus is the same dry sound at every instant.
 */

#include <stdio.h>

#include <string>
#include <utility>
#include <vector>

#include "think.h"

#include "thSoundFeat.h"
#include "thSoundFile.h"

using std::string;
using std::vector;

typedef vector<std::pair<string, float> > Args;

/* Four seconds held: a 0.5 Hz chorus has to get through two cycles. */
static const int HOLD = 172;

struct Case {
    const char *dsp;
    const char *nearer, *farther;   /* what each change is, for the report */
    Args near, far;
};

static bool features (const string &plugins, const string &dsp, const Args &args,
                      const thsound::Extractor &ex, thsound::Features &out)
{
    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    vector<float> mono;

    if (!thsound::renderNote(synth, dsp, args, 60, HOLD, thsound::TAIL_WINDOWS, mono))
        return false;

    ex.extract(mono, out);

    return out.usable();
}

int main (int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s PLUGINS DSPDIR\n", argv[0]);
        return 2;
    }

    const string plugins = argv[1], dir = argv[2];
    const thsound::Extractor ex(TH_DEFAULT_SAMPLES);

    const Case cases[] = {
        { "juno.dsp", "chorus rate x1.1", "chorus off",
          { { "chrate", 0.55f } }, { { "chorus", 0.0f } } },
    };

    int failed = 0;

    for (const Case &c : cases)
    {
        thsound::Features stock, near, far;
        const string dsp = dir + "/" + c.dsp;

        if (!features(plugins, dsp, Args(), ex, stock) ||
            !features(plugins, dsp, c.near, ex, near) ||
            !features(plugins, dsp, c.far, ex, far))
        {
            printf("FAIL %s: would not render\n", c.dsp);
            failed++;
            continue;
        }

        const double dn = thsound::Extractor::distance(stock, near).total();
        const double df = thsound::Extractor::distance(stock, far).total();
        /* A change that changed nothing -- a control renamed, so the
           render is the stock one -- would pass on any far distance. */
        const bool ok = dn > 0 && df > dn;

        printf("%s %s: %s %.2f, %s %.2f\n", ok ? "ok  " : "FAIL", c.dsp,
               c.nearer, dn, c.farther, df);
        failed += !ok;
    }

    return failed ? 1 : 0;
}
