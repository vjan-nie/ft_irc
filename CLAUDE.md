# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`ft_irc` is a 42 school project: an IRC server in **C++98** implementing RFC 2812, targeting **HexChat** as the reference client. Single-threaded, non-blocking I/O via `epoll()`. The full assignment is in `subject.txt` / `en.subject.pdf`; deeper design notes live in `DOCUMENTATION.md`.

## Commands

```bash
make mandatory            # strictly the subject's mandatory sources (pure RFC kernel)
make bonus                # mandatory + subject bonus (Bot, FILE transfer)
make                      # full (default): bonus + platform extras (PlatformBus, AuditLog, fancy console)
make re                   # Full rebuild (full tier)
make test                 # Build & run the Google Test suite (delegates to tests/Makefile)
make testclean            # Clean test artifacts
scripts/audit.sh          # Subject-compliance audit (builds all 3 tiers, C++98 token scan, single epoll check, ...)

./ircserv <port> <password>   # Run: e.g. ./ircserv 6667 mypass

# Run a single test (after `cd tests && make`):
./tests/test_runner --gtest_filter='SuiteName.TestName'
./tests/test_runner --gtest_filter='Channel*'
```

Manual smoke test with netcat: `nc -C 127.0.0.1 6667`, then send `PASS`, `NICK`, `USER ... `, `JOIN #x`, etc.

### Build tiers

The three tiers share the kernel sources and differ **only at link time**: per-tier object dirs (`obj/<tier>/`), and exactly one `src/tiers/tier_<tier>.cpp` defining `registerExtensions(Server&)` (mandatory registers nothing; bonus adds Bot + FileTransferExt; full adds those + FancyLogSink + the `FT_IRC_CONFIG`-gated AuditLog/PlatformBus). A tier marker (`obj/.tier_*`) forces a relink when switching tiers. Zero `#ifdef` anywhere. The platform extras are additionally runtime-gated: without `FT_IRC_CONFIG`, the full binary's behavior is byte-identical to the bonus tier.

## Critical build constraint

The **server** compiles under **C++98** (`Makefile`). The **test suite** compiles under **C++17** (`tests/Makefile`, required by Google Test 1.16). This means project sources in `src/` — and the vendored **`vendor/libcpp/c98/`** tier — must stay C++98-clean *while also* compiling under C++17. Do not introduce C++11+ constructs into `src/` or `vendor/libcpp/c98/`, even though `make test` would accept them. `tests/` and the rest of `vendor/` may use C++17 freely.

`vendor/googletest` and `vendor/libcpp` are git submodules — run `git submodule update --init --recursive` before building on a fresh clone. libcpp's C++98-clean modules (`str/*`, `util/config`, `term/*`, plus the dedicated `c98/` tier: `LineBuffer`, `CsvWriter`, `Reactor`, `BufferedSocket`, namespace `libcpp98`) are **compiled from source into ircserv** — no external library is linked (subject-safe). Changes to libcpp are committed inside the submodule first, then the pointer is bumped here.

## Architecture

The event loop and all command handling live in a single **`Server`** instance (`src/Server.cpp`, `include/Server.hpp`):

- `Server::run()` — the **single** `epoll_wait` call (annotated in `src/Server.cpp`; epoll lifecycle/ctl ops live behind `libcpp98::Reactor`). Client fds register for `EPOLLIN` only; `EPOLLOUT` is armed on demand — a per-tick `_epollMask` reconcile sweep in `run()` sets `EPOLLIN | EPOLLOUT` while a client has queued output and drops back to `EPOLLIN` once `_out` drains (an always-armed `EPOLLOUT` is level-triggered and would busy-loop). `EPOLLIN` → `handleClientInput`, `EPOLLOUT` → `handleClientOutput`. The loop also runs `checkTimeouts()` (PING/PONG keepalive, SENDQ sweep) and fires `onTick` on extensions every pass.
- `_clients` (`map<int fd, Client*>`) and `_channels` (keyed by **casemapped** name — display case lives in `Channel::_name`) are the live state. `Server` owns and frees these pointers, plus all registered extensions (deleted in reverse order).
- `main.cpp` ignores `SIGPIPE`, traps `SIGINT`/`SIGTERM` into `Server::isRunning`, and calls `registerExtensions(server)` (tier-dependent) before `run()`.

**Extension seam** (`include/ext/IServerExtension.hpp`): everything optional plugs in through this observer interface — hooks for lifecycle (`onServerStart`, `onTick`), client events (`onClientRegistered`, `onClientDisconnect`), channel events (`onJoin`, `onPart`), interception (`onCommand` — fired only where ERR_UNKNOWNCOMMAND would go, so extensions can add commands like `FILE` but never shadow RFC ones; `onPrivmsg` — fired per non-channel target so virtual participants claim messages; `reservesNick`), foreign fds (`ownsFd`/`onFdEvent` + public `Server::registerExternalFd` — how PlatformBus multiplexes its socket into the same epoll), and `onAudit` fan-out (`Server::audit()` → AuditLog extension). The kernel never names a concrete extension.

**I/O is fully buffered, never blocking.** `Client` delegates to `libcpp98::BufferedSocket` (512-byte line cap, 64 KiB SENDQ — overflow latches and the client is disconnected at the next sweep point, never mid-broadcast). Every extracted line passes one sanitizer stripping stray `\r`/`\0` (kills IRC line injection; `\x01` CTCP/DCC bytes pass untouched). Handlers never call `send()` directly — they queue via `Server::sendToClient`/`sendReply`/`Client::queueMessage`, drained on `EPOLLOUT`; `disconnectClient` deliberately does **not** flush before closing (see Known traps). When iterating extracted messages, code re-checks `_clients.find(fd)` after each because a handler may have disconnected the client.

**Command dispatch** (`Server::dispatchCommand`) is a linear `if (cmd == ...)` chain, split across files by category:
- `CommandRegistration.cpp` — CAP, PASS, NICK, USER, `completeRegistration` (timing-safe password check via `libcpp::str::eq_consttime`)
- `CommandChannel.cpp` — JOIN, PART; `CommandOperator.cpp` — KICK, INVITE, TOPIC, MODE (i/t/k/o/l with strtol-bounded `+l`, validated `+k`, truncated TOPIC)
- `CommandMessaging.cpp` — PRIVMSG, NOTICE, PING, PONG, QUIT
- `CommandQuery.cpp` — WHO, WHOIS, USERHOST

Dispatch enforces a **registration gate**: only CAP/PASS/NICK/USER/QUIT/PONG run before `Client::isRegistered()`. Unknown commands reach the extensions' `onCommand` before `ERR_UNKNOWNCOMMAND`.

**Casemapping**: nicks/channels/invites compare case-insensitively over ASCII (`ircEquals`/`ircToLower` in `src/IrcCase.cpp`, matching the `CASEMAPPING=ascii` 005 token). Use these — never `==` — for nick/channel comparisons.

**Numeric replies** are `#define`d string macros in `include/Replies.hpp` (also home to the limits: `MAX_MSGLEN`, `MAX_SENDQ`, `MAX_CLIENTS`, `MAX_TOPICLEN`, `MAX_KEYLEN`, `MAX_USERLIMIT`). Use them with `Server::sendReply` — don't hand-build numeric lines.

**Extensions** (all via the seam):
- **Bot** (`src/Bot.cpp`, bonus) — virtual `ircbot`; claims PRIVMSGs to its nick (`onPrivmsg`), reserves it (`reservesNick`); `!help`/`!time`/`!info`/`!joke`.
- **FileTransferExt** (`src/bonus/FileTransferExt.cpp`, bonus) — server-mediated base64 relay (`FILE SEND/ACCEPT/REJECT/DATA/END/ABORT`), relay-only (never decodes, never touches disk), flow control via `FILE WAIT` at SENDQ/2, 60 s idle abort. Protocol spec in `DOCUMENTATION.md`.
- **PlatformBus** (`src/PlatformBus.cpp`, extra) — loopback-only TCP socket in the same epoll; line protocol `AUTH <secret>` / `PUB <#chan> <type> :<msg>` injects platform events into channels. Config-gated (`FT_IRC_CONFIG` ini: `[bus]`).
- **AuditLog** (`src/AuditLog.cpp`, extra) — append-only CSV trail via `libcpp98::CsvWriter` on the `onAudit` fan-out. Config-gated (`[audit]`).
- **FancyLogSink** (`src/extras/FancyLogSink.cpp`, extra) — TermWriter console renderer installed via `Log::setSink`; the kernel's `Log` falls back to plain iostream.

## Testing

Tests use Google Test but also feed every result into **PostMan** (`vendor/PostMan.cpp`), a styled Unicode-table reporter — `tests/test_main.cpp` bridges the two via a custom `TestEventListener`. `tests/Makefile` builds all of `src/` *except* `main.cpp` (linking `tier_full.cpp` as the one `registerExtensions` definition). Protocol-level suites share `tests/TestHarness.hpp` (TCP `TestClient` + `IrcServerTest` fixture; subclass and override `portBase()` per suite, `onServerReady()` to inject probe extensions). Test files: `test_message`, `test_client`, `test_channel`, `test_bot`, `test_integration`, `test_robustness`, `test_security`, `test_filetransfer`, `test_extensions`, `test_libcpp98`. ~149 tests (see PostMan table); PostMan's leak counter is atomic and `assertNoLeaks` takes `const char*` (a `std::string` argument would count itself as a leak — keep it that way).

## Known traps

- **RST/error teardown**: real socket errors (`ECONNRESET`, etc.) are torn down
  via the `EPOLLERR|EPOLLHUP` branch in `run()`, **not** by inspecting `errno`
  after `recv`/`send` — that was removed (the subject forbids errno-driven
  control flow after non-blocking I/O syscalls; it also cured a latent EINTR
  false-positive disconnect). Caveat for future event-loop work: which branch
  reaps an RST is **timing-dependent** when `EPOLLIN` and `EPOLLHUP` arrive in
  the same event — do NOT assume `EPOLLERR|HUP` alone covered RST before that
  errno removal (`RobustnessTest.AbruptDisconnectViaRST` guards it now).
- **Deferred disconnect (T4)**: `disconnectClient()` no longer closes the fd
  synchronously. It runs `teardownClientState()` (QUIT to channel peers,
  dedup'd by fd; leave channels; extension fan-out; log/audit) immediately,
  then either finalizes right away if `_out` is already empty, or marks the
  client `pendingClose` and lets the *existing* EPOLLOUT-gated
  `handleClientOutput()` drain it — no new `send()` call site was added, the
  T2/T3 rule (poll before every send) still holds. `updateEpollInterest()`
  stops requesting `EPOLLIN` for a pending-close client (it's write-only
  until it dies), and `handleClientInput()`'s per-message loop also checks
  `isPendingClose()` so a client can't keep executing commands from the same
  already-read batch after triggering its own deferred close. A
  `PENDING_CLOSE_TIMEOUT` (`Replies.hpp`, 5s) safety net,
  `checkPendingCloseTimeouts()`, force-closes a client whose `_out` never
  drains (peer not reading) — unthrottled, every tick, keyed off its own
  `_pendingCloseSince`, never `_lastActivity` (which stops updating once
  `EPOLLIN` is stripped). That finalize forces an abortive close
  (`SO_LINGER{1,0}`) since it's giving up on undrained backlog — a plain
  `close()` there would leave the kernel trying to gracefully flush it
  forever against a peer whose window never reopens. **Resolved**: 464
  `ERR_PASSWDMISMATCH` and the 001-005 burst on immediate post-registration
  QUIT now reach the client. **Excluded, immediate close via
  `disconnectClientNow()`** (deferring would recreate the T6 frozen-reader
  scenario or write to an already-errored socket): SendQ-exceeded (3 call
  sites) and `EPOLLERR|EPOLLHUP`. **Still out of scope, still an accepted
  regression**: the MAX_CLIENTS "Server full" rejection in `acceptClient()`
  — that client is `close()`d before ever entering `_clients`, so the
  mechanism (built on `_clients` + the reconcile sweep) structurally can't
  reach it without separate work.
  - **SIGPIPE in the test harness**: `tests/` does NOT link `main.cpp`, so the
  `signal(SIGPIPE, SIG_IGN)` that `ircserv` installs never ran in
  `test_runner` — the test process had a different signal disposition than
  the shipped binary. A server-side `send()` to a socket a test had already
  `close()`d, while a large SendQ was still pending for it, killed the whole
  process with SIGPIPE (exit 141). Now installed in `tests/test_main.cpp`.
  Keep it there; it is a property of the process, not of any one fixture.
- Autodeterminded flood; overlap is structural, not a race; don't return it as fixed FLOOD_LINES.
- **Backpressure tests use a self-terminating flood**: the T6 frozen-reader
  tests (`test_robustness.cpp`) flood until the test says stop (`stopFlood`),
  bounded by a `FLOOD_CAP` safety net — NOT for a fixed `FLOOD_LINES` count.
  A fixed volume makes the overlap a race: the flood has to happen to outlast
  the probe on every machine. The 200k-line version passed locally and failed
  in the CI container. Do not "simplify" this back to a fixed count.
- **PostMan silently dropped rows above 256** (fixed in T7): `PM_MAX_ROWS` was
  a fixed-array cap and `record()` returned early past it, with no warning.
  Under `--gtest_repeat=3` the table printed "All 256 assertions passed" while
  444 tests had actually run — and a real FAIL past row 256 was swallowed,
  leaving a green table against a red exit code. `_rows` is now a
  `std::vector` with no cap. **Any `--gtest_repeat` validation done before T7
  may have read a truncated report.**
- **checkTimeouts() disconnect reasons**: SendQ-exceeded and ping-timeout are
  distinct causes collected in the same sweep; each carries its own reason
  string (fixed — both used to report "Ping timeout"). Don't re-flatten them.
- **Valgrind harness gate verifies the scenario happened, not just "no leak"**:
  `scripts/memcheck.sh --auto` drives 4 client sessions and SIGTERMs with two
  clients (C3/C4) still alive, to exercise `~Server()` teardown against live
  `_clients`/`_channels` state. The gate has THREE exit codes, not two: `0`
  clean, `97` leak (valgrind's `--error-exitcode`), `90` setup-unverified. The
  90 exists because a bare "no leak" pass is a lie if the scenario never ran —
  a broken JOIN once passed the gate green while no client actually joined.
  C3's channel membership is now confirmed via a `WHO #vgtest` reply match
  (`"#vgtest vg_c3"`, a real RPL_WHOREPLY member line — NOT bare `"vg_c3"`,
  which also appears in RPL_ENDOFWHO and would match with no channel at all)
  before the SIGTERM. Precedence: a real leak (97) always wins over a setup
  failure (90). Don't weaken the WHO match and don't turn the setup `wait_for`s
  back into cosmetic `echo`s.
- **Flood stays out of valgrind on purpose**: Memcheck is ~20-50x slower, so a
  meaningful flood takes minutes under it. The ^Z+flood leak path is covered by
  PostMan's in-process counter (`NoLeakAfterClientChurn`), not by the valgrind
  harness. Don't add it to `memcheck.sh` without re-reading the P1 audit.
- **EXIT traps and `local` under `set -u`**: a `trap ... EXIT` that references a
  `local` variable won't see it when `set -u` aborts mid-function — bash unwinds
  the local scope before running the trap, so `kill -TERM "$vg_pid"` becomes a
  no-op and the child is orphaned. Keep PIDs meant for an EXIT trap
  script-scoped (like `vg_pid`/`SETUP_FAILURES` in `memcheck.sh`), never `local`.
- **No idle-no-spin test, on purpose**: the EPOLLOUT-on-demand design (T1)
  makes busy-looping structurally impossible — `epoll_wait` has a 1000ms
  timeout and `_epollMask` only arms EPOLLOUT when `_out` is non-empty, so
  there's no unconditional-rearm path to regress. The only external check
  would be a CPU-usage assertion: flaky, threshold-arbitrary, rejected. Don't
  add one; if you think idle-spin regressed, the bug would be in the
  `_epollMask` sweep logic, testable directly — not via CPU sampling.