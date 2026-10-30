# P15 in Rust

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`, no floating point at all: the four states, the window, eight unit tests |
| `src/main.rs` | one verb on stdin, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies |

The reasoning this project is built on is in
[../c/zonetrack.h](../c/zonetrack.h) and is not repeated here, including which
source the specification comes from: step 9 gives two thresholds and the
confirmation count, figure 15.4 gives those **and** a third threshold of 1500 mm
that the sketch omits, and the figure is the more complete of the two, so the
figure is what all four implementations follow.

## What this crate adds is an arm that does not exist

The C's `switch` over the state ends with

```c
default:
    return ZONETRACK_ERR_ARGUMENT;
```

because the state lives in a struct a caller can `memset`, scribble on or copy
out of a buffer, so a value outside the four **is representable** and has to be
answered. A `match` over this enum is exhaustive over exactly four variants, so
that arm is not written here, and the compiler refuses the file if a fifth
variant is ever added without an arm for it.

The other half of `Error::Argument` is the C's null tracker and null policy,
which a reference cannot be. So **`Error::Argument` is unreachable in this
crate, in both of the ways the C can reach it**, and the oracle has no case for
it. The name is kept so the two refusals mean the same thing in all four
implementations, which is this volume's rule, and the honest half of that is
written down too: a refusal that cannot happen is not a refusal that was tested.

That is the same shape as P07's unreachable `kRoom` in C++ and P13's
`Option<u8>` replacing a flag beside a counter. The language removes a state
rather than the arithmetic:

| | What the language removes |
|---|---|
| P07 | a refusal about a buffer the caller no longer owns |
| P13 | a flag that could disagree with the counter beside it |
| P15 | **an arm for a state the type cannot hold** |

## One line counts differently and the count is the same

The C counts the window's bits in a loop, because C has no portable population
count. This calls `count_ones`. Only the count reaches the comparison, so the
difference is not in the specification, and it is recorded here rather than left
for a reader to notice.

## What is identical

The four edges of the figure and the two the figure does not draw, which are
decisions this volume made and records: approaching returns to clear if the
target leaves without confirming, and leaving returns to present if the target
comes back. The thresholds are **strict**, so a distance sitting exactly on 800
is not under it and never confirms, which two oracle cases pin. The window is
cleared on every edge, so votes gathered while approaching cannot satisfy the
exit test. `NO_TARGET` is 0xFFFF and is further away than any threshold. The
state numbering is 0, 1, 2, 3 in the order clear, approaching, present, leaving,
because a host decoder reads `[(b >> 5) & 3]` and **that numbering is the wire
format**, which a unit test asserts rather than assumes.

The policy is a parameter in all four, because the chapter's own note on the
figure calls its numbers "a starting point measured against a real target
rather than constants to be trusted". Six of the nineteen oracle cases override
them.

## The filter protocol

```text
T <far> <enter> <leave> <confirm_n> <confirm_m> <n> <distance>*n
    -> frames=N changes=N ready=0|1 states=clear,approaching,..
    -> REFUSED policy
```

**The whole sequence of states comes back, not the final one**, because a state
machine that reaches the right end through the wrong middle is wrong, and three
cases exist to pin a middle: a window cleared on an edge, a target that returns
while leaving, and a hover between the thresholds that confirms nothing.

**The change count is checked against the oracle's own states** rather than
against a number written beside them: the test counts adjacent pairs that
differ, adds one if the first state is not clear, since every tracker starts
clear, and compares that with what the filter reports. Neither implementation
can satisfy that by accident.

A second test drives the reason the hysteresis exists through the filter rather
than describing it: the same hovering target is fed to the chapter's thresholds
and to a policy whose enter and leave are one millimetre apart, and the second
must change state more often. That is a count rather than an argument.

## What is not here

The sensor, the bus, the validity filter of step 8, the occupancy reduction and
the confidence field of the status byte. The chapter gives no sigma limit, no
minimum distance, no set of trusted status codes, no bucket mapping and no
confidence definition, and **inventing six constants to fill a struct would be
fiction**: they are bench decisions by the chapter's own account. They arrive
when the X-NUCLEO-53L8A1 is read, and that shield is already stacked on the
board. There is no measured number in this crate.
