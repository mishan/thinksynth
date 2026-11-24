#!/usr/bin/env python3
"""asrscore -- how much of a sung or spoken voice a speech recognizer hears.

Renders the first four Harvard sentence lists (IEEE, 1969), one sentence
at a time, through a voice graph built on osc::speak, transcribes each
with whisper.cpp, and scores the share of each sentence's words heard in
order: their longest common run with the transcript, where a word heard
is one said the same ("four" and "for", "threw" and "through", by the
first pronunciation the CMU dictionary gives) and a number under ten
thousand is its words. A recognizer that hallucinates "la la la" over a
melisma, or says a line twice, loses nothing by it; one that hears the
wrong word does.

It is a measuring stick for changes to the speech synthesis, not a gate:
the recognizer is a separate download and runs on the CPU for a few
minutes, so nothing in ctest calls this.

    scripts/asrscore.py -b build-native --whisper PATH/whisper-cli \\
        --model PATH/ggml-small.en.bin sing.dsp sung
    scripts/asrscore.py -b build-native speak.dsp speech --set "sibilance = 0;"

`speech' is a syllable every quarter second on one pitch; `sung' is a
syllable every half second on a melody, each note let go before the next;
`legato' the same with each note held into the next, which a mono voice
such as sing.dsp slides across. --set adds chanargs to the instrument block, so a knob
can be scored at another value without editing the graph. The piece's
seed is pinned, so a render is the same every time, and so, decoding
greedily, is the transcript: a difference between two runs is the voice.

A sentence is about eight words, so at forty sentences one word is a
third of a percent of the score; a difference of a point or two is a
handful of words and should be read as such.

whisper.cpp: https://github.com/ggml-org/whisper.cpp, built with cmake;
its models are listed in its models/README.md. small.en is the one these
figures have been taken with. --whisper and --model default to
THINK_WHISPER and THINK_WHISPER_MODEL.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

SENTENCES = [
    # List 1
    "The birch canoe slid on the smooth planks.",
    "Glue the sheet to the dark blue background.",
    "It's easy to tell the depth of a well.",
    "These days a chicken leg is a rare dish.",
    "Rice is often served in round bowls.",
    "The juice of lemons makes fine punch.",
    "The box was thrown beside the parked truck.",
    "The hogs were fed chopped corn and garbage.",
    "Four hours of steady work faced us.",
    "Large size in stockings is hard to sell.",
    # List 2
    "The boy was there when the sun rose.",
    "A rod is used to catch pink salmon.",
    "The source of the huge river is the clear spring.",
    "Kick the ball straight and follow through.",
    "Help the woman get back to her feet.",
    "A pot of tea helps to pass the evening.",
    "Smoky fires lack flame and heat.",
    "The soft cushion broke the man's fall.",
    "The salt breeze came across from the sea.",
    "The girl at the booth sold fifty bonds.",
    # List 3
    "The small pup gnawed a hole in the sock.",
    "The fish twisted and turned on the bent hook.",
    "Press the pants and sew a button on the vest.",
    "The swan dive was far short of perfect.",
    "The beauty of the view stunned the young boy.",
    "Two blue fish swam in the tank.",
    "Her purse was full of useless trash.",
    "The colt reared and threw the tall rider.",
    "It snowed, rained, and hailed the same morning.",
    "Read verse out loud for pleasure.",
    # List 4
    "Hoist the load to your left shoulder.",
    "Take the winding path to reach the lake.",
    "Note closely the size of the gas tank.",
    "Wipe the grease off his dirty face.",
    "Mend the coat before you go out.",
    "The wrist was badly strained and hung limp.",
    "The stray cat gave birth to kittens.",
    "The young girl gave no clear response.",
    "The meal was cooked before the bell rang.",
    "What joy there is in living.",
]

# Notes in the piece: more than any sentence has syllables, the rest
# silent, so each render is one sentence and a pause, never two.
NOTES = 40


ONES = ("zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
        "fifteen sixteen seventeen eighteen nineteen").split()
TENS = "_ _ twenty thirty forty fifty sixty seventy eighty ninety".split()


def spelled(n):
    """`n', under ten thousand, in words."""
    if n < 20:
        return ONES[n]
    if n < 100:
        return TENS[n // 10] + ("" if n % 10 == 0 else " " + ONES[n % 10])
    if n < 1000:
        return ONES[n // 100] + " hundred" + ("" if n % 100 == 0 else " " + spelled(n % 100))
    return spelled(n // 1000) + " thousand" + ("" if n % 1000 == 0 else " " + spelled(n % 1000))


def words(text):
    text = re.sub(r"\d+", lambda m: " " + spelled(int(m.group())) + " " if int(m.group()) < 10000 else " ", text)
    return re.sub(r"[^a-z' ]", " ", text.lower().replace("-", " ")).split()


def dictionary(tree):
    """Each word in xform::say's dictionary, by how it is said."""
    said = {}
    with open(os.path.join(tree, "plugins", "composer", "cmudict.h")) as f:
        for line in f:
            m = re.fullmatch(r'\s*"([A-Z\']+) ([A-Z0-9 ]+)\\n"\s*', line)
            if m:
                said[m.group(1).lower()] = re.sub(r"\d", "", m.group(2))
    return said


def heard(ref, hyp, said):
    """How many of ref's words hyp has, in order, and how many ref has."""
    r, h = ([said.get(w, w) for w in words(t)] for t in (ref, hyp))
    run = [0] * (len(h) + 1)
    for i in range(1, len(r) + 1):
        diag = 0
        for j in range(1, len(h) + 1):
            diag, run[j] = run[j], diag + 1 if r[i - 1] == h[j - 1] else max(run[j], run[j - 1])
    return run[len(h)], len(r)


def piece(sentence, voice, mode, extra, seed):
    text = sentence.rstrip(".")
    if mode == "speech":
        notes, step, hold = "A2", 0.5, 0.45
    else:
        notes, step = "A3 C4 D4 E4 D4 C4 A3 G3", 1
        hold = 1.25 if mode == "legato" else 0.9
    return f'''name "asrscore";
tempo 120;
seed {seed};
instrument v {{ dsp "{voice}"; amp = 100; {extra} }};
chain v {{
    stage src gen::lsystem {{ axiom = "{'F' * NOTES}"; depth = 0; notes = "{notes}";
        step = {step} beats; hold = {hold} beats; vel = 100; }};
    stage w xform::say {{ words = "{text} {'_ ' * (2 * NOTES)}"; }};
    sink {{ instrument = v; }};
}};
''', NOTES * step / 2 - 0.1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("voice", help="the voice graph, e.g. sing.dsp")
    ap.add_argument("mode", choices=["speech", "sung", "legato"])
    ap.add_argument("-b", "--build", default="build", help="the build tree, for genwav and the plugins")
    ap.add_argument("--dsp-path", default=None, help="THINK_DSP_PATH for the render (default: the tree's dsp/)")
    ap.add_argument("--whisper", default=os.environ.get("THINK_WHISPER"), help="whisper.cpp's whisper-cli")
    ap.add_argument("--model", default=os.environ.get("THINK_WHISPER_MODEL"), help="a whisper.cpp model")
    ap.add_argument("--set", default="", help="chanargs for the instrument block, e.g. 'sibilance = -12;'")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--keep", help="a directory to keep each sentence's render in")
    ap.add_argument("-v", "--verbose", action="store_true", help="every sentence and what was heard")
    a = ap.parse_args()

    if not a.whisper or not a.model:
        sys.exit("asrscore: --whisper and --model (or THINK_WHISPER, THINK_WHISPER_MODEL) are needed")
    tree = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    genwav = os.path.join(a.build, "scripts", "genwav")
    plugins = os.path.join(a.build, "plugins") + "/"
    said = dictionary(tree)
    env = dict(os.environ, THINK_DSP_PATH=a.dsp_path or os.path.join(tree, "dsp"))

    got = total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for k, sentence in enumerate(SENTENCES):
            text, seconds = piece(sentence, a.voice, a.mode, a.set, a.seed)
            gen, wav, wav16 = (os.path.join(tmp, f) for f in ("s.gen", "s.wav", "s16.wav"))
            with open(gen, "w") as f:
                f.write(text)
            subprocess.run([genwav, "-q", "-m", "-p", plugins, "-s", str(seconds), "-o", wav, gen],
                           check=True, env=env)
            subprocess.run(["sox", "-q", wav, "-r", "16000", wav16], check=True)
            if a.keep:
                os.makedirs(a.keep, exist_ok=True)
                subprocess.run(["cp", "-f", wav, os.path.join(a.keep, f"{k:02d}.wav")], check=True)
            # -sns: no "(panting)" or "(music)" in place of words.
            hyp = subprocess.run([a.whisper, "-m", a.model, "-f", wav16, "-nt", "-np", "-sns"],
                                 capture_output=True, text=True, check=True).stdout.strip()
            h, n = heard(sentence, hyp, said)
            got += h
            total += n
            if a.verbose:
                print(f"{h}/{n}  {sentence}  ->  {hyp}")
    print(f"{100 * got / total:.1f}% of {total} words heard: {a.voice}, {a.mode}"
          + (f", {a.set}" if a.set else ""))


if __name__ == "__main__":
    main()
