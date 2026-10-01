# P12: energy as a regression test, and the rig that runs it

Three gates in increasing order of what they cost to run, so the cheap ones fail
first and the expensive one runs only on code that has already earned it.

**State: both gates are written and proven. The hardware job is not.** The
criterion the chapter states outright is met today:

    send: 646.60 uC against a baseline of 610.00, which is +6.0% and over the 5% tolerance
    exit 1

That is a deliberate charge regression turning the build red **with nobody at the
bench**, on a laptop with no instrument attached. 16 checks pass.

## The three gates

| Gate | What it costs | What it catches | State |
|---|---|---|---|
| 1, host unit tests | seconds, no board | most ordinary defects | **already the rest of this repository**: 71 checks across P02, P03, P05, P08, P09 |
| 2, the size gate | nothing | the change that quietly consumes the flash margin | written and proven |
| 3, the charge gate | a board, a probe and a Raspberry Pi | an energy regression | the gate is written and proven; the job that feeds it is not |

Gate 1 is deliberately not a new thing here. The chapter's architecture is that
cheap gates run on a cloud runner with no board attached, and that is exactly what
the existing suite already is. Writing a separate set would have been duplication
wearing an architecture's clothes.

## The rule that makes a gate a gate

**A missing measurement is a failure, not a pass.** A gate that skips when it cannot
find its input is not a gate: it is a gate-shaped hole that reports green on the day
the build stops producing the thing it checks. Both gates refuse rather than skip,
and the tests pin that in six separate ways.

The failures worth more than the happy path, each with its own test:

| Refused | Why it matters |
|---|---|
| a missing phase | **losing a phase looks like an improvement**: fewer phases, less total charge, everything green. The single most dangerous way for an energy gate to be wrong, because the direction of the error flatters the work |
| phases that do not sum to the total | the run has lost or double counted a phase, and no verdict on it is worth anything |
| a ledger with no build identity | a capture with no build identity is not evidence and cannot gate anything |
| a phase measured but not in the baseline | the gate would be watching less than the run does |
| a target with no committed budget | a new binary nobody is watching the size of is how the margin disappears |
| no sizes reported at all | a build failure wearing a passing gate's clothes |

The tolerance boundary is pinned from **both** sides: six percent fails, and four
and a half percent passes. A gate that failed everything would also satisfy the
first test on its own.

An improvement is not a failure. Using less charge than the baseline is the point of
the work.

## Every number in the baseline is a placeholder

`baseline.json` says so in its own `_note`, and a test asserts that it says so,
because **a placeholder that reads like a measurement is the worst artefact this
repository could contain**. Nothing here has run on hardware, so there is no measured
baseline to commit. The placeholders exist so the gate could be written and proven
before the instrument, and the first real run replaces them. That replacement is the
commit that makes this gate mean something.

`budgets.json` is different in kind: those are design decisions rather than
measurements, chosen from the chapter's figures and the memory map, and revising one
is a visible commit with a reason rather than a quiet edit.

## Running them

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python python/P12/gates.py charge captures/ledger.json
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python python/P12/gates.py size build-host/sizes.json
```

## Layout

    python/P12/gates.py                       both gates, one shape
    projects/P12-.../budgets.json             flash and RAM limits, design decisions
    projects/P12-.../baseline.json            the charge baseline, all placeholders
    python/tests/test_gates.py                sixteen checks, no hardware

Both gates are in one file because they are the same shape: a measurement, a
committed expectation, a tolerance, and a verdict that must be able to fail. Writing
them twice would invite them to drift apart in the one respect that matters, which is
what they do when the measurement is missing rather than merely large.

## Not done

- **The size gate has no real input.** It reads sizes a build reported, and no build
  for the target has ever run: there is no `arm-none-eabi-size` on this laptop. The
  budgets are committed and the gate is proven on synthetic sizes.
- **The hardware-in-the-loop job does not exist.** The chapter's architecture is a
  self-hosted runner on a Raspberry Pi that downloads the artefact the cloud built,
  flashes the board, runs the measurement and compares the ledger. Every one of those
  five steps needs something this bench has not set up yet.
- **The charge ledger has no producer.** P10 is the project that divides a cycle into
  phases with marker pins, and P07 establishes the cycle. Until both exist there is
  nothing to feed gate 3 but synthetic input.
- No watchdog work, which the chapter also covers. The subtlety it names is real and
  is not addressed here: a watchdog fed from a timer proves only that the scheduler
  runs, and will feed happily while a task deadlocks.
