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
 * serve.mjs -- the browser build's site, over HTTP on this machine.
 *
 *   node wasm/web/serve.mjs [DIR] [--port N] [--host ADDR]
 *
 * A worklet module will not load from file:, and AudioWorklet exists only in
 * a secure context, which a browser grants to https and to localhost. So
 * this serves DIR -- build-web/ at the top of the tree by default -- on
 * 127.0.0.1. Another machine on the network can reach it with --host
 * 0.0.0.0, but its browser will not count that as secure; to try the page
 * from there, forward the port and open it as localhost.
 */

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js':   'text/javascript; charset=utf-8',
    '.mjs':  'text/javascript; charset=utf-8',
    '.wasm': 'application/wasm',
    '.json': 'application/json',
    '.dsp':  'text/plain; charset=utf-8',
    '.css':  'text/css',
};

/* Resolves with the listening server. */
export function serve (root, port = 8080, host = '127.0.0.1')
{
    root = path.resolve(root);

    const server = http.createServer((req, res) =>
    {
        const url = new URL(req.url, 'http://localhost');
        let file = path.join(root, decodeURIComponent(url.pathname));

        if (file !== root && !file.startsWith(root + path.sep))
        {
            res.writeHead(403).end();
            return;
        }

        if (fs.existsSync(file) && fs.statSync(file).isDirectory())
            file = path.join(file, 'index.html');

        fs.readFile(file, (err, data) =>
        {
            if (err)
            {
                res.writeHead(404).end();
                return;
            }

            res.writeHead(200, {
                'Content-Type': TYPES[path.extname(file)] ??
                                'application/octet-stream',
                'Cache-Control': 'no-store',
            });
            res.end(data);
        });
    });

    return new Promise((resolve) =>
        server.listen(port, host, () => resolve(server)));
}

/* Run as a script, rather than imported -- and `node -e' has no argv[1]. */
if (process.argv[1] !== undefined &&
    import.meta.url === pathToFileURL(process.argv[1]).href)
{
    const here = path.dirname(fileURLToPath(import.meta.url));
    let root = path.join(here, '..', '..', 'build-web');
    let port = 8080;
    let host = '127.0.0.1';
    const args = process.argv.slice(2);

    for (let i = 0; i < args.length; i++)
    {
        if (args[i] === '--port' && i + 1 < args.length)
            port = parseInt(args[++i], 10);
        else if (args[i] === '--host' && i + 1 < args.length)
            host = args[++i];
        else
            root = args[i];
    }

    if (!fs.existsSync(path.join(root, 'thinkweb.wasm')))
    {
        process.stderr.write(`serve.mjs: no build in ${root} -- see ` +
                             'wasm/web/CMakeLists.txt\n');
        process.exit(2);
    }

    await serve(root, port, host);
    process.stdout.write(`http://${host === '0.0.0.0' ? 'localhost' : host}` +
                         `:${port}/  (serving ${path.resolve(root)})\n`);
}
