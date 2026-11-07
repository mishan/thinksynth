You are writing seven DSP plugins and one FFT for thinksynth, a C++ synthesizer,
as a clean-room implementation. The code they replace is not in this checkout,
and you must not look for it.

Read `docs/cleanroom/SPEC.md` first and in full. It is the whole of what you
are given: each plugin's interface, what it does as measured from outside, and
how your work will be checked. Then:

1. Keep to the rules in the spec's "The rules" section for the whole task.
   In short: no repository history (do not fetch, add remotes, or check out
   other branches), and no musicdsp.org, no copies of its snippets, no code
   from Embree & Kimble's textbook, and nothing quoting them. Published papers,
   books and standard DSP knowledge are fine. If a search or a page turns out to
   show one of the forbidden sources, stop reading it, note in the log that it
   happened and what you saw, and carry on without it.
2. Start `docs/cleanroom/LOG.md` before you write any code, and keep it as you
   go: each source you consult and what you took from it, and each design
   decision -- above all, how you derived or fitted every scale mapping the
   spec asks you to match.
3. Read the plugin API (`libthink/thPlugin.h`, `libthink/think.h`) and
   `plugins/filt/svf.cpp` as a model of the tree's conventions.
4. Write each plugin, build, and check it against the targets:

       cmake -S . -B build && cmake --build build
       python3 -I docs/cleanroom/measure.py --check docs/cleanroom/targets.json

   Work plugin by plugin; the build lists all seven, so it fails until each
   file exists -- a minimal placeholder that registers the right args is a
   fine way to get the rest building while you work on one.
5. When every check passes, run `ctest --test-dir build`, regenerate the
   render hashes with `cmake --build build --target corpushash-update`, and
   record in the log which shipped instruments changed hash.
6. Commit your work on this branch with a message saying what you built and
   that it was written to `docs/cleanroom/SPEC.md` without access to the
   earlier code.

Do not change the spec, the targets or the measurement script. If a target
seems impossible to meet without the earlier code, say so in the log with what
you tried, rather than loosen it.
