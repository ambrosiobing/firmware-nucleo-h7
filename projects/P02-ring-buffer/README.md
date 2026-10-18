# P02: a single producer, single consumer ring buffer

Status: c=board cpp=host python=host rust=host

The structure every later project here leans on, and the argument for why its
twenty lines are correct when one side runs in an interrupt handler and the other
runs in the main loop.

**Four implementations since Saturday 3 October 2026, and two of them are honest
about what they cannot do.** The Python joins the trace comparison and is absent
from the ordering one, because the interpreter has no release store, no acquire
load and no barrier. The Rust names a second absence: the C's one-struct-two-
contexts shape does not compile in safe Rust at all, so its crate is a correct
sequential ring and the two-context split is named as missing rather than faked.
All four were proven on the host on Saturday 3 October 2026, first in WSL on the
win11 skyhorizon demo laptop, bing@JPTOUPM678, and then in CI at commit 3b301e2.
The C++ and the Rust were the first pair in this volume to compile without a
correction: clippy clean on the first run, and every parity answer matching the
C.

`python/tests/test_ring_parity.py` compares the trace: which bytes were accepted,
which refused and counted, the order they came back in, and the two free-running
counters, over 9620 operations in each of the four ordering modes. What no host
can compare is the cost of those modes, which is this project's actual subject.

**State: runs on the board, four targets, one per ordering mode**, the producer in
thread mode and the consumer in SysTick.

**This paragraph said the opposite until Sunday 18 October 2026.** It read "written
and tested on the host, never run on the target" and called that the single most
important sentence in this directory. It was true when it was written and stopped
being true on Saturday 3 October 2026, when the four targets were built and
measured, so it stood false for a fortnight. The sentence a document nominates as
its most important one is the one most worth re-reading, and nothing about being
important makes a sentence self-maintaining.

**The ordering argument is still the deliverable and the host still cannot test
it**, which is why this is a correction rather than a tidy-up. The host figures
below prove the structure; only the board can price the barriers. That price is in
"On the board" and it overturned this project's first table.

The structure itself lives in [`c/ring/ring.c`](../../c/ring/ring.c) rather than
here, because P03 fills it from an interrupt, P04 from a transfer engine, P05
parses frames out of it and P11 runs a command queue on top of it. It belongs to
all of them rather than to whichever project first needed it. What lives here is
the apparatus that proves it: the property test, the two-thread soak and the
target demonstration.

## The invariant, which is the whole reason it needs no lock

> The consumer may read a slot only after the producer has published a head that
> includes it, and the producer may write a slot only after the consumer has
> published a tail that released it.

Each side writes one index and reads the other's. The moment a second interrupt
source calls `ring_put`, everything here stops being true, and that is the most
common way this structure fails in the field: it presents as data that is fine
for hours and then wrong once. A queue owned by a kernel is the answer there,
which is P20.

`head` and `tail` are free running counts of bytes ever written and ever read.
They are never wrapped; only the array subscript is masked. Unsigned overflow is
defined in C, so `head - tail` stays correct across the counters' own wrap, no
slot is sacrificed to tell full from empty, and the capacity is the number in the
header rather than one less. The mask and the free running counters are one
decision and not two: without a power of two the counters would have to be
wrapped at the capacity, the difference would stop being the occupancy, and the
sacrificed slot would come back.

## What the host can prove and what it cannot

| Claim | Can the host test it | Why |
|---|---|---|
| No masking or wrap fault | yes | the byte sequence on the far side |
| No index fault | yes | the occupancy against an independent tally |
| No policy fault | yes | offered equals accepted plus refused, twice over |
| Full means the capacity, not one less | yes | the boundary test |
| The counters survive their own wrap | yes | the indices are placed near the boundary and driven across |
| The capacity assertion stops a bad build | yes | compiled wrong on purpose, three ways |
| **The barriers are correctly placed** | **no** | **the host is strongly ordered** |

That last row is the point. `RING_BARRIER=0`, every barrier removed, **passes
every test in this repository**, and would pass a week-long soak. It is not
shippable. The ordering argument comes from the architecture reference manual for
this profile, from Arm's barrier application note, and from the published
correctness proof under the C11 memory model. The only machine that can falsify
it is the Cortex-M7 with the producer in an interrupt handler, which is P03.

`soak_threads.c` prints that limitation in its own output on every run, so a
reader cannot infer a guarantee it never gave.

## The four ordering modes, all buildable

Selected with `-DRING_BARRIER=N`. The book's listing offered them as commented-out
alternatives, which cannot be built and therefore cannot be compared or measured.

| Mode | What it is | Correct when |
|---|---|---|
| 0 | nothing | never. It exists so the cost of correctness is a number |
| 1 | a compiler signal fence | the counterpart is code on this core, since exception entry and return are context synchronising |
| 2 | a thread fence, one DMB | every case in this volume, including a transfer engine. The default |
| 3 | acquire load and release store on the indices | as mode 2, and it is what the correctness proof is written against |

What is deliberately absent is `volatile` on its own. It appears in most
published versions of this structure and is not sufficient: it orders volatile
accesses against each other and says nothing about the ordinary store to the data
array that must be visible before the index publishing it. The C standard gives
no ordering between a volatile and a non-volatile access.

## Running it

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python python/tools/build_host.py
```

```bash
cd C:\Users\aquamarine\Desktop\firmware-nucleo-h7; python -m pytest python/tests/test_ring.py -q -s
```

## Measured

| Quantity | Measured | Note |
|---|---|---|
| Bytes through the structure, per mode | 21,575,862 accepted of 26,277,532 offered | 200000 steps, seed recorded in `property_test.c` |
| Peak occupancy reached | 256 of 256 | the full boundary is genuinely exercised |
| Suite runtime, four modes | about 9 seconds | was 127 seconds before the loop moved into C |
| `ring.c` object size at -Os | 1347 bytes | host x86-64, **not** the flash cost |
| Cycles per put, per mode | measured, see "On the board" below | Saturday 3 October 2026, four targets, instruction cache on |

## On the board, Saturday 3 October 2026

**These figures lived only in the state table of [`projects/README.md`](../README.md)
until Sunday 18 October 2026**, while this page said the project had never run. They
are transcribed from there because that is the only record: no console capture is
committed anywhere in this repository, which is a gap this volume now names rather
than one it has closed.

**Re-measured with the instruction cache on, and that overturned the first table.**
Cold, the `DMB` build measured 2 cycles FASTER than no barrier at all, so the
comparison had the sign wrong and not merely the magnitude. The earlier claims of 7
and 17 cycles are withdrawn.

| Ordering mode | Cycles per put and get | Bytes |
|---|---|---|
| compiler fence | 0.00 | 0 |
| one `DMB` in put and get | 22.00 | 16 |
| acquire plus release | 29.00 | 24 |

**The compiler fence costs nothing and is not nothing.** It emits different
instructions of the same length, which the build tells apart by SHA-256 rather than
by size, so a barrier that costs zero cycles and zero bytes is still an observable
change to the image. A size comparison alone would have reported no difference and
been wrong about the cause.

Zero mismatches in 21.4 million bytes across the four targets.

## Three corrections to the published chapter

**The byte count is out by a factor of about twenty.** The chapter says its test
drives "two hundred thousand random bursts, a little over a million bytes". With
burst sizes uniform over 0 to 263 and two loops per step, the measured figure is
**21,575,862 bytes accepted**, 107.9 per step. The test is far stronger than
claimed, which is the good direction to be wrong in, but the number in the
acceptance criteria is not the number the test produces.

**The random number generator is weak exactly where it is used.** The chapter's
`lcg` returns its raw state and the burst size is that modulo `RING_SIZE + 8`,
which is 264, which is 8 times 33. The low three bits of a power-of-two-modulus
LCG have period 8, so by the Chinese remainder theorem the burst sizes carried a
period-8 structure. Checked before this was written: `n % 8` repeats
4, 3, 6, 5, 0, 7, 2, 1 for ever. The test crossed buffer boundaries far less
randomly than it appeared to. Replaced with xorshift32, which has no low-bit
weakness, seed recorded.

**A stated acceptance criterion was not actually checked.** "Bytes offered equals
bytes accepted plus bytes dropped, checked by the test" appears in the criteria,
and the chapter's listing accumulates a drop count and only prints it. Here it is
asserted twice, against the caller's tally and against the structure's own
counter, which are different numbers that must agree. A buffer that silently
refuses bytes looks exactly like a link that works, until the day it does not.

## Also stronger than the chapter asked for

**The capacity assertion is proved to fire on every run.** The criteria say
"verified by changing it on purpose once", and once is the problem: a check done
by hand one afternoon and then trusted is a check nobody is running.
`python/tools/check_ring_assert.py` compiles the structure with 100, 255 and 1 and
requires each to fail on the assertion, then with 2, 256 and 4096 and requires
each to succeed, because a test that only ever expects failure would pass against
a compiler that refused everything.

## Not done

- **Taken on Saturday 3 October 2026, and this bullet claimed otherwise until
  Sunday 18 October 2026.** It asked for `-DRING_BARRIER=0` against the other three
  as the measurement that would make the cost of correctness a number rather than a
  belief. That measurement exists, in "On the board" above, and it corrected a sign
  as well as a magnitude. The bullet is kept rather than deleted because what it
  asked for is exactly what the board delivered.
- The buffer's placement is a decision this project records and P19 revisits: in
  tightly coupled memory for now, which reverses the moment a transfer engine
  becomes the producer, because the main engines cannot reach that memory at all.
  The symptom is a transfer that never completes rather than an error.
- No zero-copy span pair yet. P04 needs it to hand a transfer engine a
  destination without an intermediate copy.
