# Running the relay

The room page needs a relay (`wasm/web/relay.mjs`): the shared document,
the clock, who is in a room, and signalling. One Node process, one port.
Rooms live in memory; accounts are kept in one SQLite file. A page
served over https can only open `wss://`, so the relay runs behind a TLS
proxy.

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
ends every room in progress; accounts are in the volume and survive it.

## Accounts

An account is a handle and a key: eight words the relay picks, which the
room page's Account dialog shows once for a password manager to save.
The relay keeps only hashes of keys and sessions. Anyone may still join
as a guest, marked as one, under any name that is not an account's
handle; no handle starts with `guest-`. The routes are under
`/api/account/` on the relay's own port (`wasm/web/accounts.mjs`).
Accounts belong to the relay the site's `config.json` names: a page sent
to another relay with `?relay=` joins it as a guest and keeps its
session to itself.

The image reads three variables, which `compose.yaml` passes on:

- `DB`: the file, `/data/relay.db` in the image. `compose.yaml` mounts
  the named volume `accounts` on `/data`. Run from the tree, the relay
  keeps `relay.db` beside itself; `DB=:memory:` keeps nothing.
- `CORS_ORIGIN`: the origin the room page is served from -- the Pages
  site's, `https://pages.example.org` for instance. The account API
  answers only that origin, and without it the relay offers no accounts
  (its health line says so) and the page shows no Account button. Put it
  in a `.env` beside `compose.yaml`:

      CORS_ORIGIN=https://pages.example.org

- `TRUST_PROXY`: how many proxies in front append to `X-Forwarded-For`,
  which the API's rate limits read. `compose.yaml` sets 1 for the nginx
  of `docker/nginx.conf`, which appends it. Set it only behind proxies
  that do, or a client picks its own address.

The document socket is let in by a ticket in its query string, good for
five minutes. `docker/nginx.conf` logs requests by path alone so that
tickets stay out of the access log; keep that `log_format` if the file is
adapted.

### Back it up

Losing the file loses every account, with no way to recover one: the
key is the account. Copy it daily from the host, which needs `sqlite3`.
`.backup` is safe while the relay writes:

    vol=$(docker volume inspect -f '{{.Mountpoint}}' thinksynth_accounts)
    sqlite3 "$vol/relay.db" ".backup '/var/backups/thinksynth/relay-$(date +%F).db'"

as root from cron, keeping a few weeks of copies somewhere else as well.
To restore, stop the relay, copy a backup over `relay.db` (removing any
`relay.db-wal` and `relay.db-shm` beside it), and start it again.

### Moderation

The admin commands run against the same file, beside the running relay:

    docker exec thinksynth-relay node relay.mjs admin account <handle>
    docker exec thinksynth-relay node relay.mjs admin rename <handle> <new>
    docker exec thinksynth-relay node relay.mjs admin ban <handle>
    docker exec thinksynth-relay node relay.mjs admin unban <handle>
    docker exec thinksynth-relay node relay.mjs admin revoke <handle>
    docker exec thinksynth-relay node relay.mjs admin delete <handle>

A ban ends the account's sessions and refuses its key until an unban; a
banned account cannot delete itself. `revoke` ends the sessions and
leaves the key working. A deleted account's handles -- its own and any it
was renamed from -- stay nobody's for 30 days, so a name cannot be taken
over to impersonate its owner; `delete <handle> --free` frees them at
once. The relay looks
at the sessions behind its open rooms once a minute and closes those
that have ended, so a ban or a revoke empties the account out of every
room within the minute.

## Not yet

- TURN. Peers whose NATs defeat STUN fall back to the relay forwarding
  their commands (`mesh.js`), which works but adds a hop.
- Limits on rooms. Anyone who can reach the relay can open one; the
  account API is the part that is rate-limited.
