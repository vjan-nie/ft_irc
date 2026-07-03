# Testing & QA Discipline

How this project turns "I fixed it" into "a test would catch it if it ever
came back." This document is the QA companion to `WORKFLOW.md`: the workflow
defines *how a change moves through audit → plan → review*; this defines *what
evidence a change must carry* and *how tests are written, validated, and kept
honest*.

It is a **discipline, not a platform.** It rides on what already exists — the
GoogleTest suite under `tests/` and the three-agent workflow — and deliberately
adds no new framework, runner, or "QA subsystem." If you find yourself building
infrastructure to support QA, stop: that is scope creep, and this project
already carries enough of it.

---

## 1. Principle: a test is evidence, not decoration

A bug fix is not done when the symptom disappears. It is done when a test
exists that **fails on the broken code and passes on the fixed code**. That
discrimination — red before, green after — is the only thing that proves the
test actually exercises the bug rather than passing by accident.

The corollary is uncomfortable and load-bearing: a green test proves nothing
on its own. A test that was never seen to fail might be asserting the wrong
thing, hitting the wrong code path, or be a tautology. Every regression test
must have a recorded red state.

---

## 2. The Red-Green-Review loop

This layers directly onto the three roles in `WORKFLOW.md`. The mapping is what
makes it bias-resistant: no single agent both writes the test and decides it
passes.

| Stage | Role (from WORKFLOW.md) | Responsibility |
|-------|-------------------------|----------------|
| **Red** | Auditor | Reproduce the bug *as a failing test* in `tests/`, and show it failing. The test is the audit's reproduction made executable. |
| **Green** | Implementer | Make the test pass — **without modifying the test** — while keeping the whole suite green. |
| **Review** | Adversarial reviewer | Independently validate the *test itself* as much as the code. |

The hard rule that prevents gaming: **the author of the fix may not edit,
weaken, or delete the test.** In a single-agent loop, TDD quietly degrades into
"write the test my code already passes." With separated roles, the implementer
inherits a test they cannot touch and must satisfy it honestly.

The Red test is diagnosis-as-code, so writing it does not break the auditor's
"diagnose, do not fix" posture — a failing test is not a fix.

---

## 3. Not every change is Red-Green

Forcing a failing-test-first ritual onto work that has no behavioral bug
produces awkward, dishonest tests. Match the evidence to the kind of work:

- **Bug / regression** → Red-Green. A test that reproduces the defect, shown
  failing, then passing. This is the default and the strongest case.
- **Refactor** (behavior must not change) → the existing suite stays green
  before and after. If coverage of the touched area is thin, *add
  characterization tests first* (capturing current behavior) so the refactor
  has a safety net. No new "failing" test is expected.
- **Performance / resource fix** (no wrong output, just wrong cost — e.g. the
  idle-CPU spin) → a measured **before/after number** in the PR, plus a guard
  test that asserts the resource property with a **generous margin** (see §5).
- **New feature** → tests for the new behavior, including its failure and edge
  cases. Red-Green applies per acceptance criterion where it fits.

If you cannot write a reliable failing test for a bug, treat that as a signal
that the bug is not yet understood — keep auditing, or document explicitly why
a deterministic test is not achievable (e.g. a genuine heisenbug).

---

## 4. How tests are written here

Match the existing style in `tests/` rather than inventing a parallel one:

- GoogleTest, built and run via `tests/Makefile` (`make` in `tests/`). The
  server is C++98; the test tier compiles at C++17 as GoogleTest requires.
- Protocol-level tests use the shared harness in `tests/TestHarness.hpp`:
  - `TestClient` — a real TCP client (connect, `sendCmd`, `recvAll`,
    `registerClient`, `hasNumeric`).
  - `IrcServerTest` — a fixture that runs a `Server` in a background thread.
    Override `portBase()` with a unique base per suite (grep existing bases to
    avoid bind clashes) and `onServerReady(Server&)` to inject test-only
    extensions before `run()`.
- Register every new test file in `TEST_SRCS` in `tests/Makefile`.
- Put suite-local helpers (fake extensions, etc.) in an anonymous `namespace`.
- **Assert observable behavior, not structure.** Drive the server through its
  real interface (bytes on a socket, replies, numerics, loop activity via the
  extension seam) and assert on what an actual client or operator would see.
  "The object exists" / "the count is 1" proves existence, not behavior.

Prefer reusing the architecture's own seams over adding production hooks for
testing. (The idle-spin regression test counts event-loop iterations through
the existing `IServerExtension::onTick` seam — it adds zero production code.)

---

## 5. Keeping the suite trustworthy

Flaky and slow tests are how a suite dies: people start ignoring red. Defend
against it:

- **Prefer deterministic signals over timing or %CPU.** Count iterations,
  inspect state, assert on returned bytes — not wall-clock thresholds.
- **When a threshold is unavoidable, demand a huge margin and document it.**
  (The idle-spin guard separates ~1–2 iterations/sec from >100000 with a
  threshold of 1000 — six orders of magnitude of headroom.)
- **Slow, stress, and load tests live in a separate target** (e.g. a `stress`
  target), never in the default suite. Add them only when a concrete risk
  justifies them — not speculatively.
- The default suite must stay fast and green. A red default suite means stop
  and fix, not "known flaky, ignore."

---

## 6. The reviewer's test-validity checklist

In addition to reviewing the code (see the `adversarial-reviewer` skill), the
reviewer must vet the **test** and record the result:

- [ ] **Discrimination, reproduced independently.** The reviewer ran the test
  against the unfixed code and saw it FAIL, and against the fixed code and saw
  it PASS — not trusting the author's numbers.
- [ ] **Right reason.** The red failure is caused by the actual bug, not by an
  unrelated error (compile failure, wrong port, timeout).
- [ ] **Behavioral, not structural.** The assertion reflects what a user or
  operator observes.
- [ ] **No tautology / no false positive.** The test can actually fail; it is
  not asserting something always true.
- [ ] **Margin and runtime.** Any threshold has ample headroom; the test does
  not meaningfully slow the default suite, or it lives in a separate target.
- [ ] **Untouched by the fix.** The implementer did not modify the test to make
  it pass.

A change with code that looks correct but a test that fails this checklist is
**APPROVE WITH CHANGES** at best — an unverified test is not a regression guard.

---

## 7. QA as an ongoing loop

Beyond single fixes, the same discipline scales into a repeatable habit for the
project: **audit a component → reproduce a weakness as a failing test → fix →
have the reviewer validate both → fold the lesson into `CLAUDE.md`.** Each pass
leaves the suite stronger and the institutional memory (the "known traps" in
`CLAUDE.md`) richer.

This loop is intentionally low-tech. It is the existing suite plus the existing
three-agent workflow plus the rules above — nothing to install, nothing to
maintain. Resist the temptation to grow it into a QA product. Its value is the
discipline, not the tooling.

---

*Referenced from `WORKFLOW.md`. The method is portable; adapt it. If the rules
get in the way more than they help on a given change, you are applying them to
the wrong kind of work — see the taxonomy in §3.*
