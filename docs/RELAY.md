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

The names below are placeholders: `relay.example.org` for the relay's
DNS name, `https://pages.example.org` for the origin the room page is
served from.

### The first time

1. A relay started by hand with `docker run`, from before compose, holds
   the name `thinksynth-relay` and port 8787: stop and remove it first.

       docker rm -f thinksynth-relay

2. nginx: merge `docker/nginx.conf` into the site's file by hand -- do
   not copy it over, or certbot's `listen 443` and certificate lines go
   with it. What it adds:

   - `log_format thinksynth ...;` at the top level of the file, outside
     every `server { }`;
   - `access_log /var/log/nginx/access.log thinksynth;` inside each
     `server { }` that proxies the relay, the TLS one included;
   - `proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;` in
     the `location`.

   Then

       sudo nginx -t && sudo systemctl reload nginx

   before the new relay takes traffic. It trusts one proxy's
   `X-Forwarded-For` (`TRUST_PROXY=1`), and without the header every
   client is nginx's address and shares one rate limit, so a few
   registrations hold off everybody's; the relay logs "TRUST_PROXY is set
   but the proxy sends no X-Forwarded-For" once if that happens. For a
   new host, point the DNS name at it, install the file with the real
   name in place of `relay.example.org`, and run
   `sudo certbot --nginx -d relay.example.org`. Long proxy timeouts
   matter: a document socket is quiet while nobody types.

3. Copy `docker/compose.yaml` over, with a `.env` beside it:

       CORS_ORIGIN=https://pages.example.org
       PASSKEY_RP_ID=pages.example.org

   (`CORS_ORIGIN` exactly an origin: scheme, host and any port, no path
   or trailing slash; `PASSKEY_RP_ID` the domain it is on, for passkeys
   -- see [Passkeys](#passkeys) before picking it, and leave it out for
   none. The relay will not start on anything else.) Then

       docker compose -f compose.yaml pull
       docker compose -f compose.yaml up -d

   The relay listens on `127.0.0.1:8787`, for the proxy only, and
   `curl 127.0.0.1:8787/` answers with its protocol, `accounts: true`,
   `passkeys` naming the RP ID, and its rooms. A `.env` the relay
   refuses leaves the container restarting over and over;
   `docker logs thinksynth-relay` says why.

4. Set the repository variable `JAM_RELAY` to `wss://relay.example.org`.
   The next master build writes it into the Pages site's `config.json`,
   and the demo's room page joins it. Without the variable the site has
   no relay and the room page cannot join anyone.

5. Back the accounts up (below) from the first day.

### Updating

Pages first, then the relay. A new page works on an old relay, as a
guest with no Account button; an old page is refused by a new relay
("this page is older than the relay"), so the pages people have should
be new by the time the relay is.

1. Let master's build deploy the Pages site.
2. Merge any change to `docker/nginx.conf` by hand, as above, and
   `sudo nginx -t && sudo systemctl reload nginx`.
3. `docker compose -f compose.yaml pull && docker compose -f
   compose.yaml up -d`.

Rooms live in memory, so a restart ends every room in progress; pages
in a room join it again by themselves. Accounts are in the volume and
survive it.

### Rolling back

The relay first, then Pages -- the other way round from an update, for
the same reason. In `compose.yaml`, pin the image to the previous
commit's tag in place of `latest`:

    image: ghcr.io/mishan/thinksynth-relay:<previous commit>

and `docker compose -f compose.yaml up -d`; then revert the site. A
relay from before accounts leaves the database in the volume alone, and
the accounts are there again when the relay that knows them is.

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

The image reads three variables, which `compose.yaml` passes on, and
two more for passkeys (below):

- `DB`: the file, `/data/relay.db` in the image. `compose.yaml` mounts
  the named volume `accounts` on `/data`. Run from the tree, the relay
  keeps `relay.db` beside itself; `DB=:memory:` keeps nothing.
- `CORS_ORIGIN`: the origin the room page is served from -- the Pages
  site's, `https://pages.example.org` for instance. The account API
  refuses requests from any other origin. Without it the relay has no
  accounts at all: `/api/account/` answers 404, a session in a room's
  hello is ignored (the page joins as a guest), the health line says
  `accounts: false` and the page shows no Account button. Put it in a
  `.env` beside `compose.yaml`:

      CORS_ORIGIN=https://pages.example.org

- `TRUST_PROXY`: how many proxies in front append to `X-Forwarded-For`,
  which the API's rate limits read. `compose.yaml` sets 1 for the nginx
  of `docker/nginx.conf`, which appends it. Set it only behind proxies
  that do, or a client picks its own address.

### Passkeys

A passkey logs in to the same session a key does. With passkeys on, the
Account dialog makes an account with a passkey and shows the key once as
the recovery key; one can still be made with a key alone, and the key
always logs in. Adding a passkey takes the key; removing one takes only
a session, and ends none: replacing the key is what logs out every
other browser. Replacing the key removes every passkey the account
has, since whoever had the old key could have added any of them; the
dialog then offers to add one with the new key.

A passkey asks the authenticator to verify its user (a PIN, a
fingerprint) where it can, but does not require it. So a security key
with no PIN logs in whoever holds it, as the key logs in whoever has it;
and so does one with a PIN whose passkey was made under credProtect
level 1, which lets it answer without the PIN. A session a passkey
logged in to outlasts the passkey: removing it ends none, and replacing
the key is what ends them.

The relying party is the site the page is served from, not the relay:
WebAuthn binds a passkey to an RP ID, a domain the page's own origin
must be on. Two more variables:

- `PASSKEY_RP_ID`: that domain -- `pages.example.org` for a page at
  `https://pages.example.org`, or `example.org`, which would let the
  same passkeys work on the site's other subdomains if they ever took
  them. `CORS_ORIGIN` must be on it, and is still the one origin whose
  page can log in to this relay with a passkey; the relay will not start
  otherwise. Unset, there are no passkeys: the health line's `passkeys`
  is null and the page offers none. A page on another host (`127.0.0.1`
  for a relay set up for `localhost`, say) offers none either.
- `PASSKEY_RP_NAME`: the name a passkey is saved under; `thinksynth` if
  unset.

Both go in the `.env` beside `compose.yaml` (The first time, above).

Pick the domain for good. A passkey works only on the RP ID it was made
for, so moving the site to another domain, or changing `PASSKEY_RP_ID`,
orphans every passkey made so far; their owners log in with their keys
and add new ones.

The document socket is let in by a ticket in its query string, good for
five minutes. `docker/nginx.conf` logs requests by path alone so that
tickets stay out of the access log; keep that `log_format` if the file is
adapted. nginx's error log still names the whole request, ticket and all,
when the relay cannot be reached; a ticket lapses five minutes after it
is handed out, so keep the error log to the people who run the host.

### Back it up

Losing the file loses every account, with no way to recover one: the
key is the account. Copy it daily from the host, which needs `sqlite3`;
`.backup` is safe while the relay writes. As root:

    mkdir -p /var/backups/thinksynth

and in root's crontab (`sudo crontab -e`), where `%` has to be written
`\%`:

    17 4 * * * sqlite3 "$(docker volume inspect -f '{{.Mountpoint}}' thinksynth_accounts)/relay.db" ".backup '/var/backups/thinksynth/relay-$(date +\%F).db'" && find /var/backups/thinksynth -name 'relay-*.db' -mtime +28 -delete

which keeps four weeks of copies. Keep some somewhere other than the
host as well.

### Restore

With the relay stopped, and the file the relay's user's (uid 1000 in
the image):

    docker compose -f compose.yaml stop relay
    vol=$(docker volume inspect -f '{{.Mountpoint}}' thinksynth_accounts)
    rm -f "$vol/relay.db-wal" "$vol/relay.db-shm"
    install -o 1000 -g 1000 -m 600 /var/backups/thinksynth/relay-<day>.db "$vol/relay.db"
    docker compose -f compose.yaml start relay

A restore goes back to the day of the backup, for better and worse:
keys replaced since work again and the new ones do not, passkeys
removed since -- by their owners, or with a replaced key -- log in
again and those added since do not, sessions ended
since -- logged out, revoked -- are live again, and an account banned
since is not. After one, ban those accounts again (`admin ban`), and
ask anyone who replaced a key because it was lost or seen to replace it
again; `admin revoke` ends a restored account's sessions.

### Moderation

The admin commands run against the same file, beside the running relay:

    docker exec thinksynth-relay node relay.mjs admin account <handle>
    docker exec thinksynth-relay node relay.mjs admin rename <handle> <new>
    docker exec thinksynth-relay node relay.mjs admin ban <handle>
    docker exec thinksynth-relay node relay.mjs admin unban <handle>
    docker exec thinksynth-relay node relay.mjs admin revoke <handle>
    docker exec thinksynth-relay node relay.mjs admin delete <handle>

A ban ends the account's sessions and refuses its key and passkeys until
an unban; a banned account cannot delete itself. `revoke` ends the
sessions and leaves the key and passkeys working. A deleted account's
handles -- its own and any it was renamed from -- stay nobody's for 30
days from the delete, so a name cannot be taken over to impersonate its
owner; `delete <handle> --free` frees them at once. `rename` takes the
same handles an owner could pick, so none starting with `guest-`. The
relay looks at the sessions behind its open rooms once a minute and
closes those that have ended, so a ban or a revoke empties the account
out of every room within the minute.

## Metrics

- `METRICS_PORT`: a port to serve the relay's metrics on, for a load
  test (`wasm/web/relayload.mjs`): `GET /` answers JSON with the event
  loop's delay, the worst lag of a 10 ms timer and the CPU used, all
  over the window since the last reset; memory; counts of rooms, peers
  and sockets; and messages and bytes each way by type, since the start.
  `GET /?reset` answers the same and starts a new window; `window`
  counts the resets. Have one scraper reset -- relayload's first shard
  does -- and anyone else read without it. Unset, there is no such port,
  and nothing is counted.

It listens on 127.0.0.1 alone, and never on the relay's own port: nginx
reaches that port from loopback, so a loopback check there would let
everyone in. Do not publish it from the container -- leave it out of
`ports:` in `compose.yaml` -- and read it from inside instead:

    docker exec thinksynth-relay node -e "fetch('http://127.0.0.1:9100/').then((r) => r.text()).then(console.log)"

with `METRICS_PORT: "9100"` in the service's `environment`.

## Not yet

- TURN. Peers whose NATs defeat STUN fall back to the relay forwarding
  their commands (`mesh.js`), which works but adds a hop.
- Limits on rooms. Anyone who can reach the relay can open one; the
  account API is the part that is rate-limited.
