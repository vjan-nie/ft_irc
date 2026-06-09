# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`ft_irc` is a 42 school project: an IRC server in **C++98** implementing RFC 2812, targeting **HexChat** as the reference client. Single-threaded, non-blocking I/O via `epoll()`. The full assignment is in `subject.txt` / `en.subject.pdf`; deeper design notes live in `DOCUMENTATION.md`.

## Commands

```bash
make                      # Build the ircserv binary (C++98, -Wall -Wextra -Werror)
make re                   # Full rebuild
make test                 # Build & run the Google Test suite (delegates to tests/Makefile)
make testclean            # Clean test artifacts

./ircserv <port> <password>   # Run: e.g. ./ircserv 6667 mypass

# Run a single test (after `cd tests && make`):
./tests/test_runner --gtest_filter='SuiteName.TestName'
./tests/test_runner --gtest_filter='Channel*'
```

Manual smoke test with netcat: `nc -C 127.0.0.1 6667`, then send `PASS`, `NICK`, `USER ... `, `JOIN #x`, etc.

## Critical build constraint

The **server** compiles under **C++98** (`Makefile`). The **test suite** compiles under **C++17** (`tests/Makefile`, required by Google Test 1.16). This means project sources in `src/` must stay C++98-clean *while also* compiling under C++17 — do not introduce C++11+ constructs into `src/`, even though `make test` would accept them. `tests/` and `vendor/` may use C++17 freely.

`vendor/googletest` is a git submodule — run `git submodule update --init --recursive` before `make test` on a fresh clone. (`.gitmodules` also lists an `ircd` reference submodule, not part of the build.)

## Architecture

The event loop and all command handling live in a single **`Server`** instance (`src/Server.cpp`, `include/Server.hpp`). It owns everything:

- `Server::run()` — the `epoll_wait` loop. Sockets are non-blocking; client fds register for `EPOLLIN | EPOLLOUT`. `EPOLLIN` → `handleClientInput` (read into recv buffer), `EPOLLOUT` → `handleClientOutput` (drain send buffer). The loop also calls `checkTimeouts()` for PING/PONG keepalive.
- `_clients` (`map<int fd, Client*>`) and `_channels` (`map<string name, Channel*>`) are the live state. `Server` owns and frees these pointers.
- `main.cpp` ignores `SIGPIPE` (essential — `send()` to a closed socket) and traps `SIGINT`/`SIGTERM` into `Server::isRunning`.

**I/O is fully buffered, never blocking.** Input: `Client::appendToRecvBuffer` accumulates raw bytes, then `Client::extractMessages()` splits on `\r\n` and returns only complete lines (partial TCP fragments stay buffered — this is the partial-message reassembly). Output: handlers never call `send()` directly; they call `Server::sendToClient` / `sendReply`, which append to the client's send buffer, drained on `EPOLLOUT`. When iterating extracted messages, code re-checks `_clients.find(fd)` after each because a handler may have disconnected the client.

**Command dispatch** (`Server::dispatchCommand`) is a linear `if (cmd == ...)` chain, split across files by category — when adding a command, declare it in the `Server` private section and add it to the dispatch chain:
- `CommandRegistration.cpp` — CAP, PASS, NICK, USER, `completeRegistration`
- `CommandChannel.cpp` — JOIN, PART, KICK, INVITE, TOPIC, MODE (+ channel/user mode helpers)
- `CommandMessaging.cpp` — PRIVMSG, NOTICE, PING, PONG, QUIT
- `CommandQuery.cpp` — WHO, WHOIS, USERHOST

Dispatch enforces a **registration gate**: only CAP/PASS/NICK/USER/QUIT/PONG run before `Client::isRegistered()`; everything else returns `ERR_NOTREGISTERED`. Registration completes (`completeRegistration`) once PASS + NICK + USER have all been received with the correct password.

**`Message::parse`** (`Message.cpp`) turns one raw line into `{prefix, command, params}` per RFC 2812 (handles the leading `:prefix` and the trailing `:param with spaces`). All handlers consume this struct.

**Numeric replies** are `#define`d string macros in `include/Replies.hpp` (e.g. `RPL_WELCOME` `"001"`, `ERR_NOTREGISTERED`). Use these constants with `Server::sendReply`, which formats the `:servername numeric nick params` envelope — don't hand-build numeric lines.

**Bot** (`Bot.cpp`, bonus): a virtual `ircbot` not backed by a real socket. PRIVMSG handling routes messages addressed to the bot's nick into `Bot::handleMessage`, which dispatches `!help`/`!time`/`!info`/`!joke`.

## Testing

Tests use Google Test but also feed every result into **PostMan** (`vendor/PostMan.cpp`), a styled Unicode-table reporter — `tests/test_main.cpp` bridges the two via a custom `TestEventListener`. `tests/Makefile` builds all of `src/` *except* `main.cpp` against the test binary, so handlers are tested directly. Test files map to units: `test_message`, `test_client`, `test_channel`, `test_bot`, plus `test_integration` and `test_robustness`.
