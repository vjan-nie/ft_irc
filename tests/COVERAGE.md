# ft_irc — Defense Coverage Map (42 correction sheet)

Cross-references every item on the 42 evaluation sheet against what the repo
actually proves today. Built from a scan of `tests/`, `scripts/audit.sh`, and
`src/`, current as of the T1–T3 kernel-compliance work.

**Legend:** ✅ covered · 🟡 partial · 🔴 gap / risk · 🧭 manual-only (not automatable)

Defend on the **`make mandatory`** binary — it excludes PlatformBus / AuditLog /
FancyLogSink, shrinking the surface an evaluator can question.

---

## A. Basic checks (ELIMINATORY — any failure = grade 0)

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| Makefile; compiles `-Wall -Wextra -Werror -std=c++98`; exec `ircserv` | ✅ | `audit.sh` §compile | Also `make mandatory/bonus/full` all build |
| **Only one** poll/epoll/select | ✅ | `audit.sh` §single-poll; verified 1 `epoll_wait` (Server.cpp:158) | PlatformBus multiplexes into the same epoll — no 2nd wait |
| poll called before each accept/recv/send | ✅ | architectural (all I/O is epoll-event-driven) | **Resolved (T3):** both unpolled best-effort sends removed (`disconnectClient` flush + `acceptClient` "Server full") — zero exceptions remain in the kernel |
| **errno not used to trigger action after recv/send/accept** | ✅ | `RobustnessTest.AbruptDisconnectViaRST` | **Resolved (T2):** errno branching removed from recv/send/accept; real errors reaped by the `EPOLLERR\|EPOLLHUP` branch in `run()`. Also cured a latent `EINTR` false-positive disconnect |
| fcntl only `F_SETFL, O_NONBLOCK` | ✅ | `audit.sh` §fcntl; verified (Server.cpp:104,250) | PlatformBus fcntl also compliant, but it's an extra |

---

## B. Networking

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| Starts, listens on all interfaces on CLI port | 🟡 | integration binds+connects | "All interfaces" (INADDR_ANY) is a code fact; not asserted, easy to defend |
| `nc` connects, sends, gets answers | ✅ | `IntegrationTest.*` (raw TCP `TestClient` = nc-equivalent) | |
| Reference client (HexChat) connects | 🧭 | — | **Manual before defense.** Not automatable |
| Multiple simultaneous connections, non-blocking | ✅ | `Robustness.RapidConnectDisconnect`, `CommandFlood`; `EventLoopTest` (idle no-spin) | Strong |
| Join channel; broadcast to all members | ✅ | `Integration.JoinChannel/ChannelMessage`; `Channel.BroadcastToAllMembers/ExcludesSender` | |

---

## C. Networking specials (robustness)

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| Partial commands; others keep running | 🟡 | `Integration.PartialDataReassembly`; `ClientBuffer.PartialMessagePreserved`; `LineBuffer98.FragmentedCRLF` | Reassembly proven; "other clients fine *while* a partial is pending" not explicitly interleaved |
| Kill client abruptly; server stays up | ✅ | `Robustness.AbruptDisconnect`, `RapidConnectDisconnect` | |
| Kill nc mid-command | 🟡 | `Robustness.AbruptDisconnect` | Not specifically "disconnect while a partial is buffered" |
| **^Z a reader + flood from another; no hang; drains on resume; no leaks** | 🔴 | partial: `SendQ.OverflowLatches`, `CommandFlood`, `NoLeakAfterClientChurn` | **GAP.** No integration test of a *frozen reader* being flooded (output backpressure). Directly tied to the EPOLLOUT/SENDQ design — high-value test |
| No memory leaks during operations | 🟡 | in-process leak counter (`NoLeak*`, PostMan) + `scripts/memcheck.sh` exists | Eval uses **valgrind**; in-process counting ≠ valgrind. Verify `memcheck.sh` runs the sheet's scenarios (esp. ^Z+flood) under valgrind |

---

## D. Client commands (basic)

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| Auth, NICK, USER, JOIN | ✅ | `Integration.SuccessfulRegistration/WrongPassword/NoPassword/JoinChannel` | |
| PRIVMSG with different parameters | 🟡 | `Integration.PrivateMessage/ChannelMessage` | Add error cases: no such nick (401), no text (412), multi-target |

---

## E. Channel operator commands (scored 0–5, −1 per broken feature)

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| Operator CAN: KICK / INVITE / TOPIC / MODE i,t,k,o,l | ✅ | `Integration.KickUser/InviteToChannel/TopicSetAndQuery/ChannelMode{Query,Key,Limit}`; `Channel.*Mode*`; `ModeBounds*` | Happy path well covered |
| **Regular user is DENIED operator actions** | 🔴 | — | **GAP.** Sheet explicitly checks this. No negative test asserting `ERR_CHANOPRIVSNEEDED (482)` for non-op KICK/TOPIC/MODE/INVITE |
| +i / +t enforced end-to-end (uninvited JOIN blocked; non-op TOPIC blocked) | 🟡 | mode *state* tested at unit level | Enforcement path not asserted end-to-end via TCP |

---

## F. Bonus (only if mandatory is perfect)

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| File transfer with reference client | 🟡 | `FileTransfer.*` (10 tests, incl. DCC relay) | Unit coverage strong; "with reference client" is 🧭 manual |
| A bot | ✅ | `Bot.*` (10 tests) | |

---

## G. Overarching (checked continuously during defense)

| Sheet item | Status | Notes |
|---|---|---|
| No segfault / crash for the whole defense | 🟡 | Robustness suite helps; ultimately live + valgrind |
| No leaks (heap freed before exit) | 🟡 | See C — needs a valgrind harness as a defense artifact |

---

## Prioritized action list (defense order)

**P0 — eliminatory, do first (a gap here = 0, everything else is moot):**
1. ✅ **errno-after-I/O** (A) — resolved (T2): errno branching removed from
   recv/send/accept; real errors handled by the `EPOLLERR|EPOLLHUP` branch.
   Guard: `RobustnessTest.AbruptDisconnectViaRST`.
2. ✅ **Un-polled `send()` in `disconnectClient`** — resolved via Option A
   (T3): the flush was removed, closing the last literal exception to
   "poll before every send" in the kernel. Accepted regression: 464 /
   welcome-burst no longer reach a client disconnected in the same tick
   they were queued (see `CLAUDE.md` "Known traps"). Deferred-teardown
   recovery (Option C) is tracked as future work (T4), out of this plan's
   scope.

**P1 — scored / high-value robustness:**
3. **Non-operator denial tests** (E) — the operator score is 0–5 and the sheet
   checks denial explicitly. Add `ERR_CHANOPRIVSNEEDED` negative tests.
4. **Frozen-reader + flood** integration test (C) — the marquee robustness
   scenario; showcases the EPOLLOUT/SENDQ backpressure design.
5. **Valgrind harness** (C/G) — verify/extend `scripts/memcheck.sh` to run the
   sheet's scenarios under valgrind; keep the output as a defense artifact.

**P2 — completeness / polish:**
6. Re-land `test_eventloop.cpp` (idle no-spin) — merged fix, missing its guard.
7. +i/+t end-to-end enforcement tests (E); PRIVMSG error cases (D).
8. **Manual reference-client (HexChat) checklist** (B/F) — the 🧭 items.
