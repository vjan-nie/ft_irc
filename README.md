*This project has been created as part of the 42 curriculum by dlesieur.*

# ft_irc

## Description

ft_irc is an IRC (Internet Relay Chat) server implemented in C++98. It follows the IRC protocol (RFC 2812) and is designed to work with HexChat as the reference client. The server handles multiple simultaneous clients using non-blocking I/O with `epoll()`, supports authentication, channel management, private messaging, and operator commands.

### Features

- **Authentication**: Password-protected server with PASS/NICK/USER registration
- **Channels**: Create, join, and manage IRC channels with `#` prefix
- **Private messaging**: PRIVMSG and NOTICE between users
- **Channel operators**: KICK, INVITE, TOPIC, and MODE commands
- **Channel modes**: `+i` (invite-only), `+t` (topic restricted), `+k` (key/password), `+o` (operator), `+l` (user limit)
- **Bonus: Bot** — Built-in `ircbot` that responds to `!help`, `!time`, `!info`, `!joke`
- **Bonus: File transfer** — DCC handshake relay *and* an original server-mediated
  `FILE` protocol (base64 relay with validation, flow control and timeouts)
- **Partial message reassembly**: Handles fragmented TCP data correctly
- **Ping/Pong keepalive**: Automatic timeout detection
- **Hardened**: ascii casemapping, CR/LF/NUL line-injection sanitizer, SENDQ +
  connection caps, bounded MODE/TOPIC/KEY parameters, timing-safe password check

## Instructions

### Compilation — build tiers

```bash
make mandatory  # strictly the subject's mandatory part (pure RFC kernel)
make bonus      # mandatory + subject bonus (bot, FILE transfer)
make            # full (default): bonus + optional platform extras
make clean      # Remove object files
make fclean     # Remove object files and binary
make re         # Full rebuild (full tier)
make test       # Build & run the test suite (Google Test, 138 assertions)
```

All three tiers produce the same `ircserv` binary name from the same kernel
sources; they differ only in which extensions are linked (per-tier object
dirs, one `registerExtensions()` translation unit each — see
`src/tiers/`). **Evaluation note:** the default `make` includes the extras,
but they are dead code without the `FT_IRC_CONFIG` environment variable — a
scripted session transcript is byte-identical across all three binaries.
Use `make mandatory` to demonstrate the strict subject build.

### Running

```bash
./ircserv <port> <password>
```

- `port`: The TCP port to listen on (e.g., 6667)
- `password`: Connection password required by clients

### Running with Docker

The whole stack — the C++ server plus the Claude-backed AI companion — runs
with one command. Docker and Compose v2 required.

```bash
cp .env.example .env          # then set ANTHROPIC_API_KEY (and a password)
docker compose up --build
```

This starts `ircserv` (published on `${IRC_PORT:-6667}`) and the
`ai-assistant` companion, which connects to the server over the compose
network, joins `$IRC_CHANNELS`, and answers when addressed (`!ai …`,
`assistant: …`, or a direct message). Secrets live only in the gitignored
`.env`.

Server only, no companion:

```bash
docker build -t ircserv .
docker run --rm -p 6667:6667 ircserv 6667 mypassword
```

Run the test suite in a clean container:

```bash
docker build --target test -t ircserv-test .
```

> The `ai-assistant` is a separate process (see `companions/ai-assistant/`),
> not part of the C++98 server or its 42 build — the server stays
> subject-clean and is unaware the companion is AI. See that directory's
> README for how it works.

#### Optional real-time / web tier

```bash
docker compose --profile platform up --build
```

Adds two more services: **realtime-agnostic** (a Rust WebSocket pub/sub
fan-out engine on `:4000`, with database change-capture) and
**realtime-bridge** — a companion that mirrors IRC and realtime in *both*
directions. IRC channel messages are published to realtime (so browser /
WebSocket clients and DB-CDC consumers see live IRC), and realtime events
(`pg/**`, `mongo/**`, browser publishes under `irc-in/<channel>`) are injected
back into IRC channels. Both directions are verified end-to-end. The default
`docker compose up` is unchanged; this tier is purely additive and outside the
42 build. See `companions/realtime-bridge/`.

### Connecting with HexChat

1. Open HexChat → Add a new network
2. Set server address to `127.0.0.1/6667`
3. Set the server password to the password you used when starting `ircserv`
4. Connect — you should see the welcome message

### Testing with netcat

```bash
nc -C 127.0.0.1 6667
PASS mypassword
NICK mynick
USER myuser 0 * :My Real Name
JOIN #test
PRIVMSG #test :Hello!
QUIT :bye
```

## Resources

- [RFC 2812 — Internet Relay Chat: Client Protocol](https://datatracker.ietf.org/doc/html/rfc2812)
- [RFC 1459 — Internet Relay Chat Protocol](https://datatracker.ietf.org/doc/html/rfc1459)
- [Modern IRC documentation](https://modern.ircdocs.horse/)
- [IRCv3 Specifications](https://ircv3.net/irc/)
- [HexChat documentation](https://hexchat.readthedocs.io/)
- [epoll(7) man page](https://man7.org/linux/man-pages/man7/epoll.7.html)

### AI Usage

AI (GitHub Copilot with Claude) was used as a programming assistant for:
- Planning the project architecture and identifying required IRC numerics for HexChat compatibility
- Generating boilerplate code for class declarations and socket setup
- Implementing IRC command handlers based on RFC 2812 specifications
- Debugging protocol compliance issues during testing
