# The room relay (relay.mjs), with the shipped pieces it seeds rooms
# from. Built from the top of the tree:
#
#   docker build -f relay/Dockerfile -t thinksynth-relay .

FROM node:24-slim

# The tree's layout, so that the relay finds gen/ and dsp/ one level up
# and the page's modules it shares at ../wasm/web.
WORKDIR /srv/thinksynth

# Both workspaces' manifests, which the lockfile is held against; only the
# relay's dependencies are installed.
COPY package.json package-lock.json ./
COPY relay/package.json relay/
COPY wasm/web/package.json wasm/web/
RUN npm ci --omit=dev --ignore-scripts --workspace relay \
    && npm cache clean --force

COPY relay/relay.mjs relay/accounts.mjs relay/passkeys.mjs \
     relay/wordlist.mjs relay/
COPY wasm/web/doc.js wasm/web/commands.js wasm/web/account.js \
     wasm/web/confusables.js wasm/web/
COPY gen gen
COPY dsp dsp
WORKDIR /srv/thinksynth/relay

# The accounts' file, somewhere the relay's user can write and a volume
# can be mounted over (compose.yaml): losing it loses every account.
RUN mkdir /data && chown node:node /data
ENV DB=/data/relay.db

USER node
EXPOSE 8787

HEALTHCHECK --interval=30s --timeout=5s \
    CMD node -e "fetch('http://127.0.0.1:8787/').then((r) => process.exit(r.ok ? 0 : 1), () => process.exit(1))"

CMD ["node", "relay.mjs"]
