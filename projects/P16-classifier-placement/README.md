# P16: Where the classifier runs, sensor, MCU or host

Status: c=host cpp=none python=host rust=host

**Started on the host, Tuesday 27 October 2026, and it is criterion 1.** Chapter
16's first acceptance criterion is the equivalence gate: "all three
implementations produce identical features and identical classes on every window
of the held-out set. The build fails if it does not." Step 4 says to build that
gate before any leg, "because doing it later never works because by then each
implementation has its own quiet assumption". Two of the three implementations
exist, the gate runs in the build, and none of it needed the shield.

**The chapter's three implementations are the three placements**, in the sensor,
on the microcontroller and on the host, and only the host one exists. What runs
today is the same gate over the host implementation written three times, in C, in
Python and, since Tuesday 27 October 2026, in Rust, which is this repository's
four-language rule rather than the chapter's. It is a weaker gate than the one
criterion 1 asks for and a real one: **the third language found a corner where
the first two disagreed**, which is the section below on the root mean square,
and the placement gate inherits the arithmetic already pinned by it.

**What it is.** The same classifier in three places, measured on one protocol:
in the sensor, on the microcontroller and on the host.

## What runs

| Piece | What proves it |
|---|---|
| `c/winfeat.c`, the three pinned feature groups and the bounded tree | 7 feature cases, 4 shape refusals, 5 tree cases, 6 tree refusals |
| `python/winfeat.py`, the twin, standard library only | the same cases, and the rounding the chapter permits kept unused |
| `rust/src/lib.rs`, `no_std`, and `rust/src/main.rs`, the filter | the same cases through a two verb protocol, and seven unit tests |
| `vectors.json`, the oracle | **compared exactly, with no tolerance anywhere**, because criterion 1 is identity and not closeness |

**One window is 128 samples of three axes**, which is step 3's definition, about
1.23 seconds at 104 Hz, and features are Q15 because that is what the most
constrained of the three implementations can produce.

**The three implementations reach the integer square root by three different
routes**, a bit-by-bit descent in the C, `math.isqrt` in the Python and
`u64::isqrt` in the Rust. That is deliberate: three transcriptions of one
algorithm would mostly prove the transcription was faithful, where three
algorithms agreeing on every case is what the gate is for. It is also what found
the defect below. The Rust crate declares a minimum Rust version of 1.84 where
the other ten in the workspace declare 1.75, because `isqrt` stabilised there,
and [rust/README.md](rust/README.md) says why that was preferred to a fourth
hand-rolled descent.

## The root mean square did not saturate, and peak to peak did

**This is what the gate is for, and it was found by writing the third
implementation rather than by reading either of the first two.** Both quantities
are Q15 and both can exceed what Q15 holds. The C header's sentence for peak to
peak is that a wrapped value "would read as a quiet window, which is the most
misleading answer available here". The same sentence applies to the root mean
square, and it had not been applied.

On a window with every sample at negative full scale the mean of the squares is
1073741824 and its root is exactly 32768, one above what Q15 holds:

| The root mean square of a window of negative full scale | |
|---|---|
| C, before Tuesday 27 October 2026 | **negative 32768**, from the cast to `int16_t` |
| Python, before Tuesday 27 October 2026 | **32768**, which is not a Q15 value at all |
| all three, since Tuesday 27 October 2026 | **32767**, saturated, as peak to peak already was |

The two had never been compared there because **no oracle case reached the
corner**. The oracle now carries that window as its seventh feature case, all
three implementations saturate, and the test prints the unsaturated root beside
the saturated one so it shows what was saturated rather than only the result.

The mean of the same window needs no saturation: it is exactly negative 32768,
which Q15 holds.

## The definition names the quantity and not the algorithm

Step 3 fixes the window length, the axes, the feature list and the numeric
format. **It does not fix the rounding, and in Q15 an exact gate needs it.**

| One sample of minus one in an otherwise zero window | Mean |
|---|---|
| truncated toward negative infinity, as pinned here | **-1** |
| rounded to the nearest value, which step 3 also permits | **0** |

The sum is minus one over 128 samples. Both are legitimate readings of the
printed definition, they differ by one least significant bit, and criterion 1
compares for equality. **So two implementations each obeying the chapter can fail
its own gate.** Every rounding here is therefore stated: the mean and the root
mean square both truncate toward negative infinity, and the alternative is
carried unused in `python/winfeat.py` so a test runs it rather than describing
it.

The truncation shows at both ends of the format. A window holding the extremes,
-32768 and 32767, has a root mean square of **4095 and not 4096**, because the
mean of the squares is 16776704 and 4096 squared is 16777216.

## The band energies are absent, and that is the same finding

Step 3's feature structure carries four band energies per axis "from the FFT".
**A quantity named that way cannot enter an exact gate.** The transform, its
scaling between stages, the window function if any, and the bin ranges that make
up each band are all unstated, and in Q15 two legitimate routes to the same band
energy differ in the last bits.

So this project carries the three groups whose algorithm can be pinned, the
structure says the band group is missing, and **what has to be settled before it
can be added is: the transform and its length, the scaling at each stage, the
window function or its absence, and the bin range of each of the four bands.**
Chapter 18 is where that transform is specified and measured, and it is bench
work. An approximate band energy inside an exact gate would turn criterion 1 from
a check into a tolerance nobody wrote down.

## A bounded tree has to be bounded by something

Step 5 chooses a tree of bounded depth deliberately, "because bounded depth is
bounded latency, and bounded latency is half of what makes a classifier
acceptable in a control loop". **A walk that trusts its own child indices is not
bounded.** A tree whose children point back at their parent walks for ever, and
on the board that is a missed control deadline rather than a wrong class, so the
walk takes the depth as an argument and refuses when it is exceeded. The same
tree with a leaf added returns a class, which is the control: the refusal is
about the cycle and not about the tree being small.

Peak to peak saturates at 32767 rather than wrapping, because the extremes of
Q15 are 65535 apart. A wrapped peak to peak would read as a quiet window, which
is the most misleading answer available here.

## What is not here

The dataset and its protocol, the three legs, the in-sensor program, the neural
comparison, and every latency, memory and charge number. **There is no measured
number anywhere in this project** and the features have been computed only on
synthetic windows. Criterion 1 is met for the feature groups implemented;
criteria 2 to 7 are measurements and need the parts.

**What it needs on the bench.** The X-NUCLEO-IKS4A1 shield and a Raspberry Pi
for the host leg, neither of which the inventory holds for this project.

Chapter 16 of [the book][book] is the written design.

[book]: ../../chapters/16-where-the-classifier-runs.md
