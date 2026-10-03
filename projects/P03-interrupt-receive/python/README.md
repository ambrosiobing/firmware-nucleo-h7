# P03 in Python

**State: host only, and the longest standing of the four.** `report.py` and
`loadgen.py` were written before any of the others, and the attribution in
`report.py` is what the C, C++ and Rust were written to be compared against.

| File | What it is |
|---|---|
| `report.py` | the attribution, the table, and the words a reader gets |
| `loadgen.py` | the rate ramp, which needs `pyserial` and a board |

`loadgen.py` is the one file in this project that needs hardware. It guards its
import and refuses cleanly rather than failing at the point of use, and
`requirements-hardware.txt` carries `pyserial`, which the suite never installs.

## Why Python is the right language for this half, unusually

Everywhere else in this volume Python is the harness and C is the deliverable.
Here the analysis genuinely belongs on the host: it reads a run a board produced,
attributes every byte, and prints a table and a paragraph for somebody to read.
The C, C++ and Rust implementations exist to be compared against it, not to
replace it, and the one of them that compiles for the target does so because the
same attribution could one day run as a board self-test.

## One difference the comparison records rather than hides

The counters here are Python integers, which are signed and unbounded, so
`report.py` checks for a negative counter and the other three cannot receive
one: their `uint32_t` has no negative state to check for. The Python therefore
carries two guards the others do not need, and the parity test compares only the
cases all four can represent.

## What it is proven against

Synthetic steps whose answers are known by construction, nineteen of them, driven
through every implementation by `python/tests/test_attribute_parity.py` and
compared against the C. The clean cases are not the interesting ones:

| Step | Why it is in the list |
|---|---|
| the bridge case | a tenth of the traffic never reached the peripheral and the target lost nothing. Reporting it as target loss would be the wrong conclusion, and this is why the file exists |
| the bridge and the target both lost bytes | the case the verdict **order** protects. BRIDGE is decided first, so this is BRIDGE; an implementation that decided the target first would answer LATENCY and publish a target limit from a run that never measured the target |
| overrun alone, drops alone, both | three different findings with three different fixes, and the same total loss in two of them |
| exactly at the tolerance, one byte past it | the boundary a careless edit to the tolerance would move silently |
| three counters that exceed a 32-bit sum | refused, rather than wrapped into a plausible bridge loss |
| over-accounted | the target claims more than the host sent, which is a measurement defect and is refused rather than clamped |

`first_loss` is compared too, over four ramps, because that is where the
confusion would actually be published: a ramp whose bridge gives up at a high
rate must not read as the target failing there, and all four skip bridge rows and
take the lowest rate rather than the first row.

**The list was found to have a hole, by mutation, and the hole is worth
recording.** Reordering the Python's verdicts so the target was decided before
the bridge changed no answer in the list, because no step lost bytes in both
places at once. Three files claimed that order was load bearing and nothing
checked it. Two steps were added and the order is now asserted by name, and the
same mutation turns the suite red.
