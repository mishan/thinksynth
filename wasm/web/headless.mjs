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
 * headless.mjs -- a browser test run under scripts/headless.sh by default.
 *
 *   import './headless.mjs';        -- first, before anything else
 *
 * ctest puts every test under headless.sh itself (THINK_TEST_HEADLESS in
 * the top-level CMakeLists.txt); the browser tests are run by hand, and a
 * bare `node pagetest.mjs' opened its AudioContexts on the desktop's
 * PipeWire and played through the speakers. Imported first, this runs the
 * same command again under headless.sh -- a private Xvfb and a PipeWire
 * whose one sink is a null sink -- and exits with its status, before the
 * test has done anything.
 *
 * Left as it is:
 *
 *   inside headless.sh already (THINK_HEADLESS), which is also how the
 *     second run knows not to do this again
 *   THINK_TEST_HEADLESS=0 (or off, no, false): to watch or hear a test,
 *     the same switch ctest has
 *   CI, which sets up the sink its jobs play into itself -- a PulseAudio
 *     null sink this would take the tests away from
 *   anything but Linux, where headless.sh has nothing to start
 */

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const script = path.join(path.dirname(fileURLToPath(import.meta.url)),
                         '..', '..', 'scripts', 'headless.sh');
const off = /^(0|off|no|false)$/i.test(process.env.THINK_TEST_HEADLESS ?? '');

if (process.platform === 'linux' && !process.env.THINK_HEADLESS &&
    !process.env.CI && !off && fs.existsSync(script))
{
    const run = spawnSync(script,
                          [process.execPath, ...process.execArgv,
                           ...process.argv.slice(1)],
                          { stdio: 'inherit' });

    if (run.error !== undefined)
    {
        process.stderr.write(`headless.mjs: ${run.error.message}\n`);
        process.exit(1);
    }

    process.exit(run.status ??
                 128 + (os.constants.signals[run.signal] ?? 0));
}
