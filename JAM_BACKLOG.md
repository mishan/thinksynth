# The jam backlog

What comes after the milestones in [JAM.md](JAM.md), refined against the
design, ordered by what depends on what. Nothing here is scheduled; M3 to
M7 come first. What this document is for is deciding the shape of each
idea now, so the milestones do not close a door one of them needs, and
recording which small decisions to make early because they are free
today and expensive later.

Two properties of the design do most of the work below, and each item
says how it leans on them:

- **Audio never crosses the network.** A peer renders the piece from the
  document, the transport and the gesture stream. So anyone who receives
  those three hears the session, at no cost to the musicians and almost
  none to the relay.
- **The stream is the piece.** A seeded piece plus a stamped command
  stream replays to the same tape anywhere the same build runs. So the
  stream is a recording, a broadcast, and a thing a program can read.

## 0. Roles the design does not have yet

Everything from section 2 on needs the room to know who someone is and
what they may do. Today a room has peers, seats and a document anyone
can edit. Before any audience feature:

- **Roles.** Owner, musician, spectator. The owner made the room and
  holds its settings. A musician holds a seat and may edit the document
  and send commands. A spectator may do neither; the relay refuses, not
  the page.
- **Room visibility.** Public, unlisted, private. A private room is join
  by invite link; an unlisted one by link; a public one appears in a
  list (section 4.3).
- **Moderation.** The owner can move a musician to spectator, remove a
  peer, and lock the document.
- **Names and a persistent id.** A peer id is per session. A musician
  needs something that outlives the session, or nothing in section 5 can
  attach to them later. This does not mean accounts now. It means the
  page keeps a random stable id in local storage and the relay learns to
  carry it, which is an afternoon today and a migration later.

*Decide now:* the relay enforces roles, and the room socket's `hello`
carries the persistent id. Both are small in M3's relay and awkward once
rooms are live.

## 1. The headless peer, and load

**What.** A peer with no page and no sound card: the Node wasm build,
joined through the room socket, holding a seat or not, reading the
document, sending and receiving commands, keeping a tape. It uses the
relayed path for gestures, so it needs no WebRTC, and it exposes what it
does as a library.

**Why now.** It is most of M3's protocol harness made real, and it is
what M4's late-join and apply-at-bar gates want to drive with more than
two peers. It is also the base for the model peer of section 6 and the
broadcast peer of section 4.1, so it is built once and early.

**Load testing**, which is the reason to build it before M5's deployment:

- Rooms of 2, 6, 12 and 30 musician peers; the mesh is expected to hold
  to about six and the relayed fan-out past that. Measure late commands
  per peer, and diff every peer's tape against every other's, which is
  the determinism check at scale and the only load result that matters.
- A relay carrying 50 rooms of the above.
- Document convergence under 12 peers editing at once, with the hash
  rule from JAM_M3.md section 4.3 holding.
- A late-join storm: 100 spectators arriving in ten seconds, each
  fast-forwarding, once section 2 exists.
- Clock spread across peers under load, since a loaded relay answers
  pings late and the offset estimate is what keeps origins agreeing.

**Done when** a scripted run prints those numbers and CI runs a small one
against the Node module.

## 2. Spectators

**What.** An audience that hears the session and has something to look
at.

**The design's answer.** A spectator is a peer without a seat. They load
the same site, receive the document read-only, the transport, and the
gesture stream, and render the piece in their own browser exactly as a
musician does. No audio is streamed, the musicians' mesh does not grow,
and the relay's cost per spectator is a few kilobytes a second of
fan-out. A thousand spectators is a fan-out problem, not an audio one.

What they watch is what the room page already draws: the roll filling,
knobs moving, seats lighting as they play, the editor with the musicians'
cursors, and later the composer view's mirror, which is a spectator of
the scheduler in exactly this sense.

Three things make it work:

- **Spectators sit behind a delay.** Their commands are never sent, so
  their only latency concern is never being late. A spectator applies
  everything two seconds behind the musicians and reports zero late
  commands regardless of jitter. Musicians hear each other tight;
  spectators hear a coherent piece a moment later.
- **Late join is a fast-forward** (M4). Spectators arrive whenever; that
  is what audiences do.
- **The relay sees the gesture stream.** Today musicians' gestures go
  peer to peer. A room with spectators, or with recording on, needs each
  musician's page to also send its commands to the relay for fan-out.
  That copy is the one change to the musicians' side.

**Depends on** M4, section 0, and section 1's fan-out numbers.

**Not this.** A device that cannot run the synth, or a listener who just
wants a URL, is section 4.1's problem. Spectators as defined here need a
browser that can run the page, which is any desktop and Android, and
iOS when it gets there.

## 3. What the audience sends back

### 3.1 Applause

**What.** A way for spectators to show appreciation, seen by the
musicians and by each other.

**Shape.** A `clap` message on the room socket, rate-limited per
spectator, aggregated by the relay into a count per second and fanned
out to everyone. The musicians' page shows it as a meter and a moment of
animation; spectators see the same, so a crowd feels like one.

Two rules that matter:

- **It is not a command.** Applause never enters the command stream and
  never touches the tape. It is a side channel, like chat.
- **It is stamped with the beat anyway.** The relay records applause
  against transport time, so a recording can show where in the piece
  people clapped, and so the model peer of section 6 has a signal to
  learn from. That is the cheapest reward function a music-making
  program will ever get, and it costs one field.

### 3.2 Chat

**What.** Text, for musicians among themselves and for the audience.

**Shape.** Two rooms on the room socket: the stage and the house.
Musicians see both and can post to both; spectators see and post to the
house only. The owner can mute a peer and clear the house. Nothing is
persisted beyond the session unless recording is on, and even then chat
goes into the recording only if the owner says so, since the house may
say things it did not expect to be kept.

Voice for musicians is M7's WebRTC audio track and stays separate: chat
is the text half and works for spectators, who are never in the mesh.

### 3.3 Tipping

**What.** A way for musicians to earn from an audience. Not planned.

**What it would need**, written down so nothing built earlier gets in
the way: accounts for musicians with a payout destination, a payment
processor that handles marketplace payouts and their tax reporting,
terms of service, a platform fee decision, and the persistent id of
section 0 to hang it on. None of that is code in this tree; all of it is
a service around it.

**Decide now:** only section 0's persistent id. Everything else waits for
there being an audience worth tipping.

## 4. Recordings and reach

### 4.1 Recordings

**What.** A session, kept, and playable later.

**The design's answer.** A recording is the room's initial document, the
ordered command stream with the beats each command applied at, the
transport events, and the wasm build's version, plus the side channels
the owner chose to keep: applause with its beats, and chat. It is
kilobytes a minute. Playing it back is the spectator page reading a file
instead of a socket, and seeking is a fast-forward. Rendering it to a
sound file is the Node build's `genwav` path with the stream as input,
for anyone who wants a WAV or an Opus to keep or post.

Three consequences:

- **The relay records.** It has the stream once section 2's copy exists.
  Recording is a room setting held by the owner, on for the whole
  session or not at all, and shown to everyone in the room the moment it
  is on. Musicians, not spectators, decide; a spectator who wants a copy
  can capture their own audio, and that is fine.
- **Recordings replay the intended stream**, not what any one peer
  heard. A peer that applied a command late heard something slightly
  different; the recording is the piece as sent. That is the right
  thing to keep.
- **A recording is pinned to a build.** A later build with a changed
  composer would replay a different tape. The recording names its build
  hash; the site keeps old builds under their hash for as long as it
  keeps the recordings that need them.

**Depends on** section 2's stream copy, M4's fast-forward, M5's
deployment. The format can be fixed as soon as M3 settles, since it is
the protocol with a file around it.

### 4.2 The broadcast peer

**What.** For anyone who cannot or will not run the page: a plain audio
stream of the session.

**Shape.** Section 1's headless peer with an encoder. It renders the
session's audio server-side, a spectator with speakers, and offers it as
Opus over WebRTC for low latency or HLS for scale, and can push it to a
streaming platform. This is the one place in the whole project where
audio crosses the network, and it does so as a spectator, never as a
musician.

**Depends on** sections 1 and 2. Worth having before the audience gets
large, since it is also what an iOS listener uses until iOS is real.

### 4.3 Sharing and finding

**What.** Links people post, and a place to find what is live.

**Shape.**

- A room link opens the spectator view by default; musicians join by
  invitation.
- A recording link opens the playback page.
- Both carry Open Graph metadata so a post shows the piece's name, who
  is playing, and a picture, which the roll can draw as an image at
  share time.
- An embeddable player for recordings, an iframe over the playback page.
- A "live now" list of public rooms on the site, with spectator counts,
  and the same for recent recordings.
- Restreaming to platforms goes through the broadcast peer, not the
  page.

**Depends on** M5, section 0's visibility, section 2, section 4.1.

## 5. Order

Grouped by what unlocks what. Sizes are relative to a milestone.

| | Item | Needs | Size |
|---|---|---|---|
| 1 | Roles, visibility, persistent id (section 0) | M3 | S |
| 2 | Headless peer and load testing (section 1) | M3, before M5 | M |
| 3 | The recording format, fixed (section 4.1) | M3 | S |
| 4 | Spectators, with the relay fan-out and the delay (section 2) | M4, 1, 2 | M |
| 5 | Applause and chat (section 3) | 4 | S |
| 6 | Recordings, kept and played back (section 4.1) | 4, 3, M5 | M |
| 7 | Sharing and the live list (section 4.3) | 6 | M |
| 8 | The broadcast peer (section 4.2) | 2, 4 | M |
| 9 | The model peer, over the headless one (section 6) | 2, M4 | M |
| 10 | Tipping (section 3.3) | an audience | a service |

Items 1 and 3 are small enough to do inside M3's tail. Item 2 gates M5.

## 6. The model peer

Discussed separately: a language model as a peer over the headless one
of section 1, composing by editing the document and rehearsing edits
against the Node renderer before applying them at a bar. Recorded here
so its dependencies are in the table: the headless peer, M4's
apply-at-bar, and section 3.1's beat-stamped applause as its feedback.

## 7. Scale, in both directions

Neither is needed soon. Both are recorded because each has one small
decision that is free in M3 and a migration later, and because both
come out of code the backlog already builds.

### 7.1 Many spectators: a tree of relays

A relay that carries a thousand spectators carries a thousand secondary
relays just as well, and each of those carries a thousand spectators.
Two levels is a million; a third is never needed.

Timing survives the extra hop for two reasons the design already has.
Commands carry `at` in transport seconds, and a relay forwards the bytes
without touching the stamp, so a command means the same thing after any
number of hops. And spectators apply everything behind a delay, so a hop
that adds tens of milliseconds is invisible under two seconds; the late
counter stays the guard and stays at zero. The one thing that compounds
is the clock estimate, a fraction of a millisecond a level between
datacenter hosts and a few over the public Internet, which is nothing
under the delay and never on the musicians' path.

A secondary is the relay with an upstream, and it is a spectator of its
parent:

- **Document:** a Yjs client to the parent, a Yjs server to its children,
  read-only downward, which Yjs does natively.
- **Gesture stream:** received as a spectator, sent as a root, in order
  and unchanged.
- **Clock:** answered in root time, its offset to the parent added to its
  own monotonic clock, so children never know the tree exists.
- **Late join, served locally:** it keeps the document snapshot, the
  last transport start and the command log since the origin, so a
  joining spectator never touches the root. The root then only ever
  talks to secondaries.
- **Upstream aggregated:** applause summed per second per secondary,
  house chat rate-limited and batched.
- **Assignment:** a spectator asks the root where to join, and the root
  hands out a secondary with room, from the loads they report.
- **Failure:** a secondary's spectators fall back to the root, are
  reassigned, and fast-forward in. A gap of seconds, nothing else.

For a truly large passive audience the broadcast peer (4.2) pushing
audio through an ordinary CDN is the conventional answer and needs none
of this. The tree is for the audience that watches, renders locally and
claps; the broadcast peer is a spectator of whichever relay is nearest,
so both coexist.

### 7.2 Many rooms: sharding

Rooms share nothing. No cross-room state, no cross-room clock, no
cross-room ordering, so a room lives entirely on one relay and the fleet
is many relays with a rule for which one.

- **Each relay is the root clock for its rooms.** Nothing compares clocks
  across relays. Sharding costs timing nothing.
- **Assignment is stable for the room's lifetime.** A live room is
  in-memory state, and moving it means everyone reconnects and
  fast-forwards back in. Rendezvous or consistent hashing over the fleet
  moves only the rooms of a relay that left, and a fleet change is a
  maintenance event.
- **Resolution happens in one place.** The page asks one endpoint which
  relay a room is on. Today it answers with the one relay; later with
  the hash.
- **Persistence sits behind the shards.** Documents at rest and
  recordings in shared storage; a relay is stateful only for live rooms.
- **A standby is a spectator.** A hot standby for a room is another
  relay subscribed to it, holding the document and the log, exactly like
  a secondary in 7.1. Promotion is reassignment. Without one, a shard
  failure restarts its rooms from the persisted document with a new
  origin, and the recording has a seam, which is acceptable for a long
  time.

One Node process carries thousands of musician rooms, so this is for
tens of thousands of concurrent sessions.

## 8. Decisions to make early

The short list of things that are free now and costly later, gathered
from above:

1. The relay enforces roles; `hello` carries a persistent id.
2. Musicians' pages send a copy of their commands to the relay when the
   room has spectators or recording on.
3. The recording format is the protocol plus a header naming the build
   hash, and the site keeps builds by hash.
4. Applause carries a beat.
5. Rooms have an owner from the first M3 deployment, even if the owner
   can do nothing yet.
6. The page resolves a room's relay through one endpoint rather than
   reading a single relay from `config.json`, and a relay's room state
   is a value another relay can be handed, so an upstream is a client
   connection and not a redesign.
