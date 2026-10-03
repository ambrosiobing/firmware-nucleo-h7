# P03: receiving on interrupt without losing bytes

Status: c=host cpp=written python=host rust=written

Three receive paths on one board with one switch between them, both documented
library failure modes reproduced on purpose and then repaired, and a measurement
that says where bytes first go missing and **which side of the link lost them**.

**Four implementations of the attribution since Saturday 3 October 2026**, which
is the half of this project that can be proven. The receive path needs a
peripheral and still refuses; the attribution is arithmetic over counters a run
recorded, and it is the half where a wrong answer publishes a wrong conclusion
rather than merely failing to work. `attribute.c` was written for this and
compiles unchanged for the target. The C++ and the Rust are **written and not
built** until the WSL run; the Python, `report.py`, is the one they were all
written to be compared against.

`python/tests/test_attribute_parity.py` drives nineteen synthetic steps and four
ramps through every implementation. The step list was itself found to have a hole
by mutation: reordering the verdicts so the target was decided before the bridge
changed no answer, because no step lost bytes in both places at once, while three
files claimed that order was load bearing. Two steps were added and the order is
now asserted by name.

**State: the measurement is written and proven; the firmware is written and never
compiled.** The half that needs no hardware is finished and tested. The half that
needs the board waits on three downloads and on P01.

## What is proven today, with no board

`python/report.py` attributes every byte the host sent to exactly one place,
and `python/tests/test_report.py` holds it to that over eleven cases whose answers
are known by construction. **11 passed.**

That ordering is the point, and it is P06's lesson applied here: the analysis was
written and tested before any byte crossed a wire, because a method written after
the first plot is a rationalisation of whatever the plot showed.

### Why one "bytes lost" figure would be worse than no figure

The easiest way to publish a wrong conclusion about a serial link is to report a
single lost-byte count. The common finding, that a link fails above some rate, is
usually a statement about the **bridge** between the host and the board, not about
the board. So every sent byte lands in one of four places:

| Attribution | What it means | The fix it points at |
|---|---|---|
| `delivered` | the target handed it to the application | none needed |
| `overrun` | the peripheral had it and the handler was too late | shorten the handler or raise its priority |
| `dropped` | the handler read it and the ring was full | a larger ring or a faster consumer |
| `bridge_lost` | it never reached the peripheral at all | nothing about the target; the run measured the bridge |

**`overrun` and `dropped` are never summed.** One is a latency failure and the
other a throughput failure, they have opposite fixes, and a combined figure points
at neither.

A ramp whose every step reports `BRIDGE` has found nothing about the target, and
`first_loss()` returns `None` rather than naming a rate. A target that accounts
for more bytes than the host sent is refused outright, because that is a
measurement defect, a counter not reset between steps, and clamping it would turn
the defect into a plausible-looking row.

### The tests were themselves tested

Four deliberate defects, all caught:

| Mutation | Caught by |
|---|---|
| fold `bridge_lost` into `target_lost`, the wrong conclusion | 2 tests |
| clamp a negative bridge loss instead of refusing | 1 test |
| treat a `BRIDGE` row as a target finding | 1 test |
| collapse latency and throughput into one verdict | 2 tests |

## The three paths, built in full

Selected with `-DRX_PATH=polled|flag|ring`. The application above `c/rx.h` is
byte identical across all three, so the comparison is of mechanisms rather than of
three programs.

| Path | Mechanism | Prediction |
|---|---|---|
| `polled` | the loop reads the data register when the flag is set | loses bytes as soon as the loop is slower than the line |
| `flag` | the interrupt stores one byte and sets a flag | loses a byte whenever a second arrives before the loop takes the first, which at any real rate is immediately |
| `ring` | the interrupt pushes into P02's ring | the one that keeps the bytes |

`ring` is the producer half of P02's structure. P02's invariant applies unchanged:
**exactly one context may call `ring_put`**, and that is this handler and nothing
else. A second interrupt source calling it breaks every guarantee P02 argued for,
and the symptom is data that is fine for hours and then wrong once.

The handler is fifteen lines and must stay that way. No `printf`, which is not
reentrant. No parsing, which is P05's job.

## The two failure modes, documented and not invented

Both come from the silicon vendor's own community, reported over years, with the
register-level cause in the threads. Neither is reproduced from library source
here: the call pattern is described, the symptom named, and the repair written at
register level so it can be read and checked.

**Mode one, the re-arm window.** The library's interrupt-driven receive takes a
count, and when that many bytes have arrived it **disables its own receive
interrupt** and calls a completion callback. Every byte arriving between that
moment and the next call to re-arm is lost, and nothing records it: no interrupt
was enabled, so no overrun is flagged either. It survives every test written at a
comfortable rate and appears under load.

*The repair, which is what `rx_ring.c` does:* never disable the receive interrupt.
One byte per interrupt, pushed somewhere that cannot block. There is then no
window to lose a byte in, because there is no completion.

**Mode two, the uncleared overrun.** The library's error path returns the
peripheral to ready **without clearing the overrun flag**, so the interrupt
condition is still true when the handler returns, the handler re-enters
immediately, and the main loop never runs again. It presents as a board that has
stopped rather than as an error.

*The repair:* clear the flag explicitly, in the handler, on the error path, before
returning. On this family the clear is a write to a dedicated register, not a read
of the status register as older families allowed. **That difference is exactly what
the most widely read worked example of this problem gets wrong, because it is
written for an older family.** That example also has no licence file, so default
copyright applies: it is read and re-derived, never vendored.

Enabled only with `-DRX_FAULT=rearm|overrun`. The default build has neither, because
a repository whose default build contains a deliberate defect is a trap for whoever
clones it next.

## What the firmware refuses

Every path returns a negative value from `rx_start` rather than running with a
guessed peripheral setting. Four things are unconfirmed and each fails differently:

| Unconfirmed | How it fails | Needs |
|---|---|---|
| USART register offsets | a write goes elsewhere; nothing receives | RM0455 |
| status flag bits | the handler tests the wrong bit, so never reads or never clears | RM0455 |
| interrupt enable bits | the interrupt never fires, which looks like a wiring fault | RM0455 |
| the NVIC position | the interrupt reaches the wrong handler, which looks like a dead peripheral | RM0455 |
| the virtual COM port pins | the wrong pins become a serial port, silently | MB1363 |
| the baud divider | needs the clock, which P01 has not established | RM0455, then P01 |

## Layout

    c/rx.h              one interface, three paths, and the counters
    c/rx_ring.c         the path that keeps the bytes
    c/usart3_ll.h       register-level receive, and every refusal
    c/faults.c          the two documented modes, and both repairs
    python/report.py    the attribution: four places, never one figure
    python/loadgen.py   the rate ramp, written, never run
    python/tests/test_report.py   eleven cases, no hardware

## Not done

- `rx_polled.c`, `rx_flag.c`, `usart3_ll.c`, `stats.c` and `main.c` are not
  written yet. `rx_ring.c` is, because it is the one every later project uses.
- `loadgen.py` cannot run: `pyserial` is not installed, and `run_step` raises
  rather than inventing a target protocol nobody has reviewed. Resetting the
  counters and reading them back needs a command the firmware does not have.
- No measured rate for where loss begins, so `docs/results.md` does not exist.
  Inventing a plausible one would be the worst outcome available here.
- The host end is a USB bridge inside the debug probe whose own limits are not
  documented anywhere this project can cite. When a step reports `BRIDGE` the
  honest statement is that the bridge did not carry the traffic, not that the
  board could not.
