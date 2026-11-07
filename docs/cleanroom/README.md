# The clean-room rewrite

Seven plugins (`filt::moog`, `res1pole`, `ink2`, `res2pole`, `allpass`,
`resonator`, `dist::waveman`) and `osc::pad`'s FFT came from outside the
project: snippets posted to musicdsp.org and the `#musicdsp` IRC channel, and
an FFT from Embree & Kimble's DSP textbook. This branch is set up for someone
who has never seen that code to write replacements from a specification.

| File | What it is | Who it is for |
| --- | --- | --- |
| `SPEC.md` | each item's interface and measured behavior, and the rules | the implementer |
| `PROMPT.md` | the instructions to give an agent doing the work | the implementer |
| `measure.py`, `targets.json` | measures plugins from outside, and the targets recorded from the code being replaced | the implementer's checks |

## Why this branch has no history

`cleanroom-base` is an orphan branch: one commit with no parent, holding the
tree with those files removed and with the few comments elsewhere that
described them rewritten. Cloning only this branch gives a checkout from which
the earlier code cannot be reached.

## Running it on another machine

1. Clone only this branch, into a directory of its own:

       git clone --single-branch --branch cleanroom-base \
           https://github.com/mishan/thinksynth.git thinksynth-cleanroom

   Check that `git log` shows one commit and `git branch -a` shows nothing
   else.
2. Start the agent there with nothing that knows the earlier code: a fresh
   session, no memory or notes carried over from sessions that worked on
   thinksynth's plugins, and no `CLAUDE.md` or other context mentioning them.
3. Give it `PROMPT.md` as its instructions.
4. When it is done, review `LOG.md`: the sources it used, and how it fitted
   each mapping.

## Bringing the result back

The orphan branch shares no history with `master`, so do not merge it.
Copy the results onto a branch from `master` instead: the seven plugin
files, `padFft`'s body, `LOG.md` and the regenerated `scripts/corpushash.txt`.

Who wrote the specification matters too: it was written by a session that had
read the earlier code, so it says only what can be measured from outside, and
the measurements are in `targets.json` for anyone to repeat.
