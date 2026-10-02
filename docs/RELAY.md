# Running the relay

The room page needs a relay (`wasm/web/relay.mjs`): the shared document,
the clock, who is in a room, and signalling. One Node process, one port,
nothing persisted. A page served over https can only open `wss://`, so
the relay runs behind a TLS proxy.

## The image

CI's `relay` job builds `docker/relay.Dockerfile` on every run, joins a
room on it, and from master publishes `ghcr.io/mishan/thinksynth-relay`
as `latest` and as the commit. The image holds the relay, its npm
dependencies, and `gen/` and `dsp/`, which new rooms are seeded from.

## On the host

1. Copy `docker/compose.yaml` over and start it:

       docker compose -f compose.yaml pull
       docker compose -f compose.yaml up -d

   The relay listens on `127.0.0.1:8787`, for the proxy only.
   `curl 127.0.0.1:8787/` answers with its protocol and rooms.

2. Point a DNS name at the host, install `docker/nginx.conf` with that
   name in place of `relay.example.org`, and run `certbot --nginx` for it.
   Long proxy timeouts matter: a document socket is quiet while nobody
   types.

3. Set the repository variable `JAM_RELAY` to `wss://<that name>`. The
   next master build writes it into the Pages site's `config.json`, and
   the demo's room page joins it. Without the variable the site has no
   relay and the room page cannot join anyone.

To update, pull and `up -d` again. Rooms live in memory, so a restart
ends every room in progress.

## Not yet

- TURN. Peers whose NATs defeat STUN fall back to the relay forwarding
  their commands (`mesh.js`), which works but adds a hop.
- Limits. Anyone who can reach the relay can open rooms.
