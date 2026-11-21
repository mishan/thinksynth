# A phone as a controller

A phone's tilt and touches played as a HID device, `phone`, on the page
open on a computer, and a configurator that sets what any controller's
controls do. This is the design; what is built is marked as it lands.

## What it is

The computer's page shows a QR code. The phone opens it, taps Start
(iOS asks before a page reads motion, and only inside a tap), and is
then a controller of that page: the same `thhMapper` the Index and the
camera play through turns its state into notes, bends, aux values and
knobs, under the device's own profile and the player's changes to it
(docs/HID.md).

Out of scope: the phone as a synth of its own (it already joins a room
as a player); bends and aux values heard by a room's other players,
which no controller has yet (docs/JAM.md, "Controllers are not read
yet"); the native desktop app, which has no network client; a bend per
finger, since a bend moves every note a device sounds (thhMapper); and
TURN, so a phone whose network defeats STUN plays through the relay.

## Why the relay

A page reads motion only in a secure context, so the phone's page is
https, and neither page can listen for a connection. So the two meet at
the relay, over the TLS the room page uses, and then talk over a WebRTC
data channel: direct on a local network, where the latency is won, and
through the relay when WebRTC can't connect.

## Pairing: `/pair` on the relay (built)

A WebSocket path beside `/doc/` and `/room/`, not a room: a room carries
a document, seats and a place in the public list, none of which a
pairing wants, and a pairing holds exactly two.

- The computer opens `/pair`. The relay makes the code, 128 random bits
  in base64url, so no page chooses a guessable one, and answers
  `{type:'paired', code, token}`.
- The link is `phone.html#pair=CODE`, a page of its own beside
  `index.html` and `jam.html`, shown as a QR code drawn by
  `qrcode-generator` (MIT, from npm, bundled as the page's other
  dependencies are). The code is in the fragment, so the site's server
  never sees it. The phone uses its own site's relay (config.json), never
  one a link names, so a link can't send a player's touches elsewhere.
- The phone opens `/pair/CODE` and gets its own token, which it keeps in
  sessionStorage. The code is then spent: another socket on it is
  refused. The host hides the QR code and says "phone joined" with what
  the phone said it is, and Disconnect.
- A side that drops reopens `/pair/CODE` and sends its token as its
  first message, never in the URL, where proxies log it. A socket
  without its side's token is refused, so a link seen after the phone
  joined plays nothing, and a phone tab iOS reloaded rejoins with the
  token it kept.
- An unjoined code dies after 2 minutes, so a QR code on a stream or a
  projector is short-lived; New code makes another. A pairing ends when
  either side has been gone 10 minutes, after a day in any case, or at
  once when either side ends it (`{type:'end'}`: the host's Disconnect,
  the phone's), which closes both.
- What either sends, `{type:'signal'|'relayed', data}`, reaches the
  other unread, under a frame cap of 4 KiB and a rate bucket for each
  side that its reconnects draw on too, and is dropped past it, or while
  the other side has 64 KiB it hasn't read: a late frame is worth
  nothing.
- Its own limits, not a room's (`PAIR` in `relay/pair.mjs`): live
  pairings on the relay and per client, new ones from their own bucket,
  and sockets waiting for their hello per client; and where `CORS_ORIGIN`
  is set, the Origin must be it, so a socket with none is refused too.

Nothing is kept: pairings are in memory, as rooms are.

## The connection

`mesh.js`'s peer connection through an adapter with a room's shape over
the pair socket (`peer`, `peers`, `on`, `signal`, `relayed(data, to)`,
`joined`, `left`), so ICE restart, the open deadline and the fall back to
`relayed` are the room's own, tested code. The mesh gains a send on one
channel: a frame that changes which fingers are down goes on both
channels, as every room command does, and the rest on the unordered one
only, since a late reading is worse than none. Through the relay, a
frame is skipped while the socket's `bufferedAmount` is above 0, so a
slow path plays the newest frame rather than a queue of old ones.

## What the phone sends

A snapshot, not events: each frame is the whole state, so a lost one is
made good by the next and no note waits on one.

    { id, seq, axes: [tilt_x, tilt_y, x1, y1, ..., x5, y5], down }

`id` is random per page load and `seq` counts within it; the host takes
a frame only if it is later than the last of its `id`, and a new `id`
starts afresh. About 60 a second while anything changes and for 200 ms
after, and 4 a second while nothing does, which is the heartbeat; tilt
changes under 0.01 are not changes, or sensor noise would never let it
rest. A finger's axes are set in the frame its finger lands in.

The host takes 1 second without a frame (4 heartbeats) as the phone
gone, checked as each frame arrives and on a timer, since a hidden tab's
timers run late: `connected` false, so the mapper lets every note go, as
a controller unplugged. The phone doesn't wait for that when it can say
so: hidden or closing (`visibilitychange`, `pagehide`), it sends a frame
with nothing down, on both channels.

## The device

`hid/thhPhone`: `thhPhoneControls()` and the state from a frame.

- Axes: `tilt_x` and `tilt_y`, bipolar, -1 to 1, the gravity vector
  (`accelerationIncludingGravity`, whose sign is checked on iOS and on
  Android) in the screen's own axes: right and up as the screen is held,
  by `screen.orientation.angle` (`window.orientation` where that is all
  there is), with the angle held while a finger is down so a turn of the
  phone doesn't jump a note. Euler angles lose roll near upright. And
  `touch_1_x` .. `touch_5_y`, 0 to 1 across and up the screen.
- Buttons: `touch_1` .. `touch_5`, a finger down.
- A finger takes the lowest free slot when it lands and keeps it until
  it lifts or is cancelled (`pointercancel`), so a second finger never
  becomes the first.
- The phone page: one full-screen surface, `touch-action: none`, no
  pull to refresh, a Wake Lock while paired, and both motion permissions
  asked for inside Start's tap. Motion refused or absent, it says so and
  touches play.

The default profile, `THH_PHONE_PROFILE`, a theremin under one finger,
for `theremin.dsp` as the hands' is (docs/CAMERA.md):

    device phone {
        base = "C3";
        touch_1 = note 0;
        touch_1_x = bend { scale = pentatonic; range = 15; smooth = 0.05; };
        touch_1_y = aux 3 { min = 1; max = 0; smooth = 0.03; };
        touch_2 = octave 1;
        touch_3 = octave -1;
        tilt_x = aux 1 { smooth = 0.05; };
    };

Across is the scale, up is loud (aux 3 is the hush), a second finger a
step up an octave and a third one down, and tilting right brightens.

## The configurator

A form over the profile language, for any device, the phone first and
then the Index, camera and hands. The language stays the one source of
truth: the form writes the player's profile text, and the text box
stays beside it. Each device keeps its own (`thinksynth:hid:DEVICE`).

- A row for every control the device has, bound or not, with its value
  now, and under it each thing it does: an axis may drive several. Each
  is an action (a note, hold or toggle a layer, octave, pitch, velocity,
  strike, bend, aux, loop, or a patch knob chosen from the patch's) and
  the options that action takes, no others. Add binds one more.
- Learn: the next control moved is the one shown.
- The device's own bindings show as its own; editing one writes yours
  over it; Reset drops yours of that control and any `ignore` of it; Off
  drops yours and writes `ignore`, which reaches bindings outside layers
  only, so a layer's binding of it is shown as still there.
- A binding the patch's `controller` block overrides is shown masked.
- It edits the block for either hand; `left` and `right` blocks, and
  layers, are shown and left to the text for now.

Edits are splices, as `thcGenEdit`'s are: the parser records the byte
span of each binding, `ignore` and setting (layers later), a change
replaces exactly that span, and a new statement goes before the block's
closing brace, so the player's comments and layout survive.
`hid/thhProfileEdit` holds this without a toolkit, as `thhPane` holds
the pane, so the desktop's Controller pane can have the same form later.
`thcGenEdit`'s span helpers (`Edit`, `applyEdits`, `endOfBlock`,
`eraseStmt`, `gapBefore`) move to libthink for both.

## Failure

- WebRTC never opens: `relayed` through the pair socket, slower but
  playing, retried as the mesh retries.
- The pair socket drops: an open channel goes on playing; the side
  reopens with its token and signalling resumes.
- The phone's first socket drops before its token arrives: the code is
  spent, so the host offers a new one.
- A lost frame: the next carries the same state. A lost heartbeat: three
  more before anything is let go.
- A frame malformed, the wrong length or not finite: dropped, counted,
  not played.
- The host page reloaded: the pairing is gone with it; the phone says so.

## Tests

- `relay/pairtest.mjs`: a code issued, the phone joining, frames both
  ways, a second phone refused, two racing for one code, a rejoin with a
  token and without, expiry and the 10-minute end on an injected clock,
  the frame cap, the rate bucket and the caps, End closing both, the
  Origin; sockets killed mid-stream.
- `scripts/phonecheck`: frames to state (orientation turned and held,
  slots kept and cancelled), late frames and a new `id`, malformed ones,
  the watchdog letting notes go and a lost heartbeat not; the default
  profile on `theremin.dsp`: a note at the landing finger's pitch, not
  the last one's, louder up the screen, an octave tapped many times.
- `scripts/hideditcheck`: splices keep comments; set, replace, add,
  reset and off; a statement into an empty block and a one-line one; any
  binding written and read back equal, over generated bindings.
- `wasm/web/phonetest.mjs`: a host page and a phone page on a local
  relay, touches and motion made up on the phone, notes and bends on
  the host; a lift whose unordered frame is lost; hidden and back; the
  channel cut and notes let go; the relayed path with WebRTC blocked;
  motion refused.
- `wasm/web/configtest.mjs`: the form edits the text it should, Learn
  finds the control moved, comments survive, a masked binding shown.
