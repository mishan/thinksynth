thinksynth on macOS
===================

If macOS says "thinksynth.app is damaged and can't be opened"
------------------------------------------------------------

It is not damaged. macOS is refusing to run it because the app is not
signed with an Apple Developer ID, and anything downloaded from the
internet arrives with a "quarantine" flag that makes Gatekeeper insist
on one.

Drag thinksynth.app to Applications, then run this once in Terminal:

    xattr -dr com.apple.quarantine /Applications/thinksynth.app

After that it opens normally, and you will not have to do it again.

If you would rather not run a command you do not understand: `xattr -dr
com.apple.quarantine` removes the "this came from the internet" marker
from the app and everything inside it. It does not change the program,
and it affects nothing else on your Mac. You can see the flag for
yourself before and after with:

    xattr -l /Applications/thinksynth.app


Why the app is not signed
-------------------------

Signing an app so that macOS accepts it without this step requires a
paid Apple Developer account, and notarising it means uploading each
build to Apple for scanning. thinksynth is a hobby project revived from
2004 and has neither.

The app *is* signed, but ad-hoc: a signature that proves the file has
not been altered since it was built, with no identity behind it. That is
what lets it run at all on Apple Silicon, where the kernel refuses
unsigned code outright. It is not what Gatekeeper wants to see for a
download.


Audio and MIDI
--------------

Audio goes out through CoreAudio and MIDI comes in through CoreMIDI;
both are found automatically. `thinksynth -L` from Terminal lists the
audio APIs, the output devices and the MIDI input ports it can see,
which is the first thing to check if it is silent.

macOS will ask for permission the first time something wants your
microphone or a MIDI device. thinksynth does not record audio.
