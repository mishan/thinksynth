#!/usr/bin/env node
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
 * make-confusables.mjs -- wasm/web/confusables.js, from Unicode's
 * confusables.txt (UTS #39):
 *
 *   curl -O https://www.unicode.org/Public/security/latest/confusables.txt
 *   node scripts/make-confusables.mjs confusables.txt
 *
 * Only the single characters whose skeleton is Latin letters and digits
 * are kept, marks in the skeleton dropped: a handle is faked with letters
 * that pass for a-z and 0-9, and the rest of the file is the rest of
 * Unicode. Then, for each letter whose capital folds apart from it once
 * the case is gone (`I' is `l'; `i' is itself), the small letter goes
 * where the capital does, so that case still does not count.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const text = fs.readFileSync(process.argv[2] ?? 'confusables.txt', 'utf8');
const version = /^# Version: (.*)$/m.exec(text)?.[1] ?? '?';
const date = /^# Date: (.*?),/m.exec(text)?.[1] ?? '?';
const year = /^# \u00A9 (\d{4})/m.exec(text)?.[1] ?? '?';

/* The data is Unicode's, under the Unicode License v3
   (https://www.unicode.org/license.txt), whose notice has to go with every
   copy of it: the generated file carries it. */
const NOTICE = `UNICODE LICENSE V3

COPYRIGHT AND PERMISSION NOTICE

Copyright \u00A9 1991-${year} Unicode, Inc.

NOTICE TO USER: Carefully read the following legal agreement. BY
DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.

Permission is hereby granted, free of charge, to any person obtaining a
copy of data files and any associated documentation (the "Data Files") or
software and any associated documentation (the "Software") to deal in the
Data Files or Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, and/or sell
copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that either (a)
this copyright and permission notice appear with all copies of the Data
Files or Software, or (b) this copyright and permission notice appear in
associated Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
THIRD PARTY RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or other
dealings in these Data Files or Software without prior written
authorization of the copyright holder.`;
const map = new Map();
const chr = (hex) => String.fromCodePoint(parseInt(hex, 16));

for (const line of text.split('\n'))
{
    const m = /^([0-9A-F]+) ;\t([0-9A-F ]+) ;\tMA\b/.exec(line);

    if (m === null)
        continue;

    const target = m[2].trim().split(' ').map(chr).join('')
        .replace(/\p{M}/gu, '');

    if (/^[A-Za-z0-9]+$/.test(target) && chr(m[1]) !== target)
        map.set(chr(m[1]), target);
}

/* As account.js's foldName does, with the table so far. */
const skeleton = (s) => Array.from(s, (c) => map.get(c) ?? c).join('');
const fold = (s) =>
    skeleton(skeleton(s.normalize('NFKD')).toUpperCase().toLowerCase());

for (const c of 'abcdefghijklmnopqrstuvwxyz')
    if (fold(c) !== fold(c.toUpperCase()))
        map.set(c, fold(c.toUpperCase()));

const entries = [...map].sort(([a], [b]) => a.codePointAt(0) -
                                            b.codePointAt(0))
    .map(([c, t]) => `${c.codePointAt(0).toString(16)}=${t}`);
const lines = [];

for (const e of entries)
{
    if (lines.length === 0 || lines.at(-1).length + e.length > 70)
        lines.push(e);
    else
        lines[lines.length - 1] += ` ${e}`;
}

const header = fs.readFileSync(path.join(here, '..', 'wasm', 'web', 'doc.js'),
                               'utf8').split('\n').slice(0, 17).join('\n');

fs.writeFileSync(path.join(here, '..', 'wasm', 'web', 'confusables.js'),
`${header}

/*
 * confusables.js -- written by scripts/make-confusables.mjs from Unicode's
 * confusables.txt, version ${version} (${date}); do not edit.
 *
 * Each character that passes for Latin letters or digits, and what it
 * passes for: hex code point, then its skeleton.
 *
 * The notice above is this project's, for the code. The table is derived
 * from Unicode's data, which is Unicode's, and is distributed under its
 * own license:
 *
${NOTICE.split('\n').map((l) => ` * ${l}`.trimEnd()).join('\n')}
 */

export const SKELETON = new Map(\`
${lines.join('\n')}
\`.trim().split(/\\s+/).map((e) =>
{
    const [hex, to] = e.split('=');

    return [String.fromCodePoint(parseInt(hex, 16)), to];
}));
`);

process.stdout.write(`confusables.js: ${map.size} characters, Unicode ` +
                     `${version}\n`);
