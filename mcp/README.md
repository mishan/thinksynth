# thinksynth MCP server

Tools for a model that writes music it cannot hear: what the tree offers,
whether a piece or an instrument loads, what it played, how it measures, and
what it costs to render. Every answer comes from the tree's own harnesses
(`genwav`, `gencheck`, `dspcheck`, `dsplevel`, `dspsweep`, `dspmatch`,
`dspcatalog`, `dspnodes`, `panelcheck`), so the server, the command line and
the program agree about what a piece means.

## Running it

Build the tree, then install the server's two dependencies:

```sh
cmake -S . -B build && cmake --build build
cd mcp && npm ci
```

Add it to Claude Code:

```sh
claude mcp add thinksynth -- node /path/to/thinksynth/mcp/server.mjs
```

Or to any MCP client, as a stdio server running `node mcp/server.mjs`.

| Variable | Default | What it is |
| --- | --- | --- |
| `THINK_BUILD` | `build/` beside `mcp/` | the build directory whose `scripts/` and `plugins/` are used |
| `THINK_MCP_OUT` | `$TMPDIR/thinksynth-mcp` | where renders, stems and tapes are written |
| `THINK_MCP_KEEP` | 32 | how many renders, auditions and matches are kept; older ones are deleted once no call is using them, and their render ids forgotten. Other calls' files go when the call ends |

## Tools

Pieces and instruments go in as text, or as a path relative to the tree; a path outside the tree and `THINK_MCP_OUT` is refused.

| Tool | What it answers |
| --- | --- |
| `catalog_instruments` | the shipped `.dsp` instruments; with `controls`, each one's chanargs |
| `catalog_stages` | every `gen::` and `xform::` plugin and its params |
| `catalog_nodes` | every DSP node and its args, for writing a graph |
| `catalog_pieces` | the shipped pieces, as worked examples |
| `piece_check` | load errors by line, and the knobs, chains, instruments and sections declared |
| `piece_render` | a WAV and a stem per instrument; loudness per instrument and section; the sections as they played; knobs moved on a timeline, as a game moves them |
| `piece_tape` | every note by name, bar and beat, per instrument |
| `piece_compare` | how much each instrument changes when one knob moves |
| `audio_analyze` | per section, bar or span: loudness, brightness, low/mid/high balance, busyness, per instrument from the stems, and findings such as "strings is 21 LU under toms" |
| `piece_profile` | render cost against real time, worst window, and each instrument's share |
| `dsp_check` | whether a graph loads, renders finite audio, and stays safe across every control's range |
| `dsp_levels` | peak against voice count |
| `dsp_audition` | notes at velocities, each measured: peak, brightness at the front and after, time to fall 40 dB |
| `dsp_sweep` | one control across its range, measured the same way |
| `dsp_match` | distance from a target recording, or the chanargs fitted to it |

There is no tool for a running instance of the program yet: the desktop
application has no control channel to reach.

## Tests

`npm test` here, or `ctest -R mcp` from the build, which sets `THINK_BUILD`.
They start the server over stdio and check each tool's claim against the
fixture in `test/` and the shipped `kit_tom.dsp`.
