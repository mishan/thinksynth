# The room relay (wasm/web/relay.mjs), with the shipped pieces it seeds
# rooms from. Built from the top of the tree:
#
#   docker build -f docker/relay.Dockerfile -t thinksynth-relay .

FROM node:24-slim

# The relay finds gen/ and dsp/ two levels up from itself.
WORKDIR /srv/thinksynth/wasm/web

COPY wasm/web/package.json wasm/web/package-lock.json ./
RUN npm ci --omit=dev --ignore-scripts && npm cache clean --force

COPY wasm/web/relay.mjs wasm/web/doc.js wasm/web/commands.js \
     wasm/web/account.js wasm/web/accounts.mjs wasm/web/wordlist.mjs ./
COPY gen /srv/thinksynth/gen
COPY dsp /srv/thinksynth/dsp

# The accounts' file, somewhere the relay's user can write and a volume
# can be mounted over (compose.yaml): losing it loses every account.
RUN mkdir /data && chown node:node /data
ENV DB=/data/relay.db

USER node
EXPOSE 8787

HEALTHCHECK --interval=30s --timeout=5s \
    CMD node -e "fetch('http://127.0.0.1:8787/').then((r) => process.exit(r.ok ? 0 : 1), () => process.exit(1))"

CMD ["node", "relay.mjs"]
