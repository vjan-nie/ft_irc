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
| **^Z a reader + flood from another; no hang; drains on resume; no leaks** | ✅ | `Robustness.ThirdClientUnaffectedByFrozenReaderFlood` / `ServerSurvivesFloodAgainstFrozenReader` / `FrozenReaderEventuallyDisconnectedOnSendQ` (T6, self-terminating flood, asserts isolation/survival DURING the flood); leak side covered by `memcheck.sh --auto` (T6/P1) | Was a 🔴 gap; closed by T6. The three tests overlap the flood structurally, not as a timing race | partial: `SendQ.OverflowLatches`, `CommandFlood`, `NoLeakAfterClientChurn` | **GAP.** No integration test of a *frozen reader* being flooded (output backpressure). Directly tied to the EPOLLOUT/SENDQ design — high-value test |
| No memory leaks during operations | ✅ | `scripts/memcheck.sh --auto` (P1): drives register/JOIN/PRIVMSG/PART/QUIT/abrupt-disconnect + SIGTERM with clients still alive, under valgrind, 3-way exit-code gate (0/97/90). Fire-drill-verified in both directions. In-process `NoLeak*` counter (PostMan) covers the flood path | Was 🟡 (in-process counting ≠ valgrind); now backed by an actual valgrind gate |
---

## D. Client commands (basic)

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| Auth, NICK, USER, JOIN | ✅ | `Integration.SuccessfulRegistration/WrongPassword/NoPassword/JoinChannel` | |
| PRIVMSG with different parameters | ✅ | `Integration.PrivateMessage/ChannelMessage`; `Integration.PrivmsgNoRecipientGivesErrNorecipient/PrivmsgNoTextGivesErrNotextToSend/PrivmsgNoSuchNickGivesErrNosuchnick`; 404 via `Integration.JoinInviteOnlyDeniedWithoutInvite` | Happy path + 411/412/401/404 all covered. Multi-target (comma-separated targets, `ERR_TOOMANYTARGETS`) is **not implemented** — out of mandatory scope, not a gap |

---

## E. Channel operator commands (scored 0–5, −1 per broken feature)

| Sheet item | Status | Proven by | Notes / gap |
|---|---|---|---|
| Operator CAN: KICK / INVITE / TOPIC / MODE i,t,k,o,l | ✅ | `Integration.KickUser/InviteToChannel/TopicSetAndQuery/ChannelMode{Query,Key,Limit}`; `Channel.*Mode*`; `ModeBounds*` | Happy path well covered |
| **Regular user is DENIED operator actions** | ✅ | `Integration.KickDeniedForNonOperator/ModeDeniedForNonOperator/TopicDeniedForNonOperatorWhenRestricted/TopicAllowedForNonOperatorWhenNotRestricted/InviteDeniedForNonOperatorWhenInviteOnly/InviteAllowedForNonOperatorWhenNotInviteOnly` | Negative tests assert `ERR_CHANOPRIVSNEEDED (482)` for non-op KICK/MODE/TOPIC(+t)/INVITE(+i); positive tests confirm TOPIC/INVITE still succeed for non-op when the channel isn't restricted |
| +i enforced end-to-end (uninvited JOIN blocked) | ✅ | `Integration.JoinInviteOnlyDeniedWithoutInvite` (paired with `InviteToChannel` for the positive case) | Uninvited non-member gets ERR_INVITEONLYCHAN (473) on JOIN; ERR_CANNOTSENDTOCHAN (404) on PRIVMSG confirms non-membership |
| +t enforced end-to-end (non-op TOPIC blocked) | ✅ | `Integration.TopicDeniedForNonOperatorWhenRestricted` | Non-op TOPIC after `MODE +t` gets ERR_CHANOPRIVSNEEDED (482) over TCP; row was stale, this enforcement was already proven |

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
| No leaks (heap freed before exit) | ✅ | `memcheck.sh --auto` is the defense artifact; `~Server()` teardown confirmed clean across mandatory/bonus/full tiers | Was 🟡; valgrind harness now exists (P1) |

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
   they were queued (see `CLAUDE.md` "Known traps"). Deferred-teardown recovery (Option C) — DONE (T4, merged): the 464 now reaches the client before close; "Server full" remains out of scope.

**P1 — scored / high-value robustness:**
3. ✅**Non-operator denial tests** (E) — the operator score is 0–5 and the sheet
   checks denial explicitly. Add `ERR_CHANOPRIVSNEEDED` negative tests.
4. ✅**Frozen-reader + flood** integration test (C) — the marquee robustness
   scenario; showcases the EPOLLOUT/SENDQ backpressure design.
5. ✅**Valgrind harness** (C/G) — verify/extend `scripts/memcheck.sh` to run the
   sheet's scenarios under valgrind; keep the output as a defense artifact.

**P2 — completeness / polish:**
6. ✅ **Idle no-spin** — no test, by design. The busy-loop the EPOLLOUT-on-demand
   fix (T1) prevents is structural, not a runtime check that could silently
   regress: `epoll_wait` blocks with a 1000ms timeout (Server.cpp:158), so the
   loop wakes at most once/sec when idle, and the `_epollMask` reconcile sweep
   (Server.cpp:344-348) only ever arms EPOLLOUT while `_out` has queued data.
   There is no code path that re-arms EPOLLOUT unconditionally to guard against.
   A black-box CPU-usage test would be the only external way to assert "not
   spinning", and it is deliberately rejected: machine/CI-dependent threshold,
   flaky, and guarding a property the architecture already enforces. No
   test_eventloop.cpp ever existed in history — this line previously implied a
   lost guard; it was never written.
7. ✅ **PRIVMSG error cases** (D) — 411/412/401 added; 404 was already covered by
   `JoinInviteOnlyDeniedWithoutInvite`. Multi-target intentionally excluded:
   `ERR_TOOMANYTARGETS` doesn't exist in the codebase, and comma-separated
   targets are out of the mandatory subject's scope, not a missing test.
8. **Manual reference-client (HexChat) checklist** (B/F) — the 🧭 items.
