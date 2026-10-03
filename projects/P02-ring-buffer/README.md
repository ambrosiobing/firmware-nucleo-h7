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

**State: written and tested on the host, never run on the target.** The ordering
argument is the deliverable and the host cannot test it, which is the single most
important sentence in this directory. See below.

The reason it has not run on the target changed on Friday 2 October 2026 and is
worth stating so it is not assumed. It used to be that no cross toolchain existed
anywhere. One does now, and five firmware targets build clean with it. What is
missing is narrower: no firmware target compiles `c/ring/ring.c`, because the
project that would exercise it from an interrupt is P03 and that has not been
built either. So this is waiting on a target of its own rather than on tooling.

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
| Cycles per put, per mode | not measured | needs the target and P01's toolchain |

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

- Never compiled for the target, so no cycle counts. `-DRING_BARRIER=0` against
  the other three is the measurement that makes the cost of correctness a number
  rather than a belief, and it needs P01.
- The buffer's placement is a decision this project records and P19 revisits: in
  tightly coupled memory for now, which reverses the moment a transfer engine
  becomes the producer, because the main engines cannot reach that memory at all.
  The symptom is a transfer that never completes rather than an error.
- No zero-copy span pair yet. P04 needs it to hand a transfer engine a
  destination without an intermediate copy.
