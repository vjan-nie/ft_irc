# realtime-bridge

A bidirectional bridge between `ircserv` and
[`realtime-agnostic`](https://hub.docker.com/r/dlesieur/realtime-agnostic) — a
Rust WebSocket pub/sub fan-out engine with database change-capture (CDC).

It lets IRC channels and a web/real-time tier share the same activity:

- **IRC → realtime** — every channel message is published to realtime-agnostic
  (`POST /v1/publish`, topic `irc:<channel>:chat/message`), so browser /
  WebSocket clients see live IRC traffic.
- **realtime → IRC** — the bridge subscribes (WebSocket `/ws`) to inbound topics
  (DB CDC `pg/**` `mongo/**`, browser events `irc-in/**`, …) and injects each
  event into the matching IRC channel. A **chat** event (payload has `from` +
  `text`) is delivered by a *puppet*: a short-lived IRC connection that
  registers the web user's own nick, so the message appears in IRC as that user
  — not as the bridge. Non-chat **system/CDC** events are injected by the main
  `rtbridge` client.

It is a **companion process** — not part of the C++98 `ircserv` binary or its
42 build. The main connection registers as `rtbridge`; each active web user
gets its own puppet connection.

## Puppets (one IRC nick per web user)

When `web-alice` publishes a chat message, the bridge opens a puppet that
registers nick `web-alice` (derived to a valid ≤9-char IRC nick, with `433`
collisions resolved by a numbered suffix) and sends the message as itself.
Puppets are reused per web user and **quit after `RT_PUPPET_TTL_SECS` idle**;
the pool is capped at `RT_PUPPET_MAX` (beyond which chat falls back to
`<from> text` via `rtbridge`). Set `RT_PUPPETS=false` to disable puppeting
entirely (everything then injects via `rtbridge`). Puppets are write-only, so
only `rtbridge` ever publishes IRC → realtime (no double-publish, no loop).

## How it stays loop-free

IRC-sourced events are published under the `irc:**` namespace, which the bridge
**never** subscribes to. A producer that wants to push *into* IRC publishes
under `irc-in/<channel>` (or includes `payload.channel`). The bridge also
ignores PRIVMSGs from its own nick.

## Inbound contract (realtime → IRC)

Publish to realtime-agnostic and the bridge relays it into IRC:

```sh
curl -X POST http://localhost:4000/v1/publish -H 'content-type: application/json' \
  -d '{"topic":"irc-in/general","event_type":"chat.message",
       "payload":{"from":"web-alice","text":"hello from the browser"}}'
# → in #general, from a puppet registered as web-alice:
#   :web-alice!web-alice@host PRIVMSG #general :hello from the browser
```

`pg/**` / `mongo/**` CDC events land in `RT_DEFAULT_CHANNEL` as
`[<event_type>] {json}` (or `<from> text` when the payload looks like chat).

## Run

```sh
export IRC_HOST=127.0.0.1 IRC_PORT=6667 IRC_PASS=yourpass
export IRC_CHANNELS="#general"
export RT_PUBLISH_URL=http://127.0.0.1:4000/v1/publish
export RT_WS_URL=ws://127.0.0.1:4000/ws
cargo run --release
```

Usually you run the whole stack with Compose instead — see the repo root:
`docker compose --profile platform up --build`.

### Environment

| Variable             | Default                              |
| -------------------- | ------------------------------------ |
| `IRC_HOST`           | `127.0.0.1`                          |
| `IRC_PORT`           | `6667`                               |
| `IRC_PASS`           | (empty)                              |
| `IRC_NICK`           | `rtbridge`                           |
| `IRC_CHANNELS`       | (none) comma-separated               |
| `RT_PUBLISH_URL`     | `http://realtime:4000/v1/publish`    |
| `RT_WS_URL`          | `ws://realtime:4000/ws`              |
| `RT_INBOUND_TOPICS`  | `pg/**,mongo/**,irc-in/**`           |
| `RT_DEFAULT_CHANNEL` | `#general`                           |
| `RT_PUPPETS`         | `true` (per-user nicks; `false` = via rtbridge) |
| `RT_PUPPET_MAX`      | `50` (max concurrent puppet connections) |
| `RT_PUPPET_TTL_SECS` | `120` (idle lifetime before a puppet quits) |
