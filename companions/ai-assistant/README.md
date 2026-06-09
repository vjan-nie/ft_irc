# ai-assistant

An IRC companion for `ircserv` that answers when addressed, via the Claude API.

It connects as an ordinary IRC client (default nick `assistant`), joins the
configured channels, and replies **only** when a message is addressed to it:

- `!ai <question>`
- `assistant: <question>` (or `assistant, ...` / `assistant ...`)
- any direct `PRIVMSG` to the bot

It keeps a short rolling context per channel and never reacts to its own
messages. It is a **companion process** — not part of the C++98 `ircserv`
binary or its 42 build.

## Run

```sh
export ANTHROPIC_API_KEY=sk-ant-...
export IRC_PASS=yourserverpassword
export IRC_CHANNELS="#general,#support"
cargo run --release
```

### Environment

| Variable            | Default           | Notes                                  |
| ------------------- | ----------------- | -------------------------------------- |
| `IRC_HOST`          | `127.0.0.1`       | ircserv host                           |
| `IRC_PORT`          | `6667`            | ircserv port                           |
| `IRC_PASS`          | (empty)           | server password (required by ircserv)  |
| `IRC_NICK`          | `assistant`       | the bot's nick                         |
| `IRC_CHANNELS`      | (none)            | comma-separated channels to auto-join  |
| `ANTHROPIC_API_KEY` | (required)        | Claude API key                         |
| `ANTHROPIC_MODEL`   | `claude-opus-4-8` | model id                               |

## Notes

- Uses the raw Claude Messages API over HTTPS (`reqwest`) — there is no official
  Rust SDK. Replies are kept concise and chunked to IRC line limits.
- Outbound IRC lines funnel through a single writer task, so model calls (which
  take a few seconds) never delay `PING`/`PONG` and the bot won't ping-timeout.
- Pairs naturally with the platform event bus: the bus posts events into a
  channel, a human asks `assistant: summarise the last few orders`, and the bot
  answers in-channel.
