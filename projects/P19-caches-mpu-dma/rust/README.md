# P19 in Rust

**State: host, since Friday 30 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`, unsigned integers only: the range rounding, criteria 4 and 5, seven unit tests |
| `src/main.rs` | three verbs on stdin, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies, and which two library calls set the version floor |

The four findings this project is about are in
[../c/mpualign.h](../c/mpualign.h) and are not repeated here.

**P19 is the last of the twenty to reach a third language**, and the only one
whose C half already runs on the board: `c/board/icache.c` has been flashed
since Saturday 3 October 2026. This crate is the half that can be compared,
which is the same half the C and the Python compare.

## What this crate adds is the wrap check moving into the type

The C has to widen to 64 bits so that the test itself cannot wrap, and says so
in its own comment:

```c
const uint64_t last = (uint64_t) addr + (uint64_t) len;
const uint64_t aligned_end = (last + mask) & ~mask;
if (aligned_end >= 0x100000000ull) {
    return MPU_ERR_WRAP;
}
```

That is correct, and it answers a question about 32 bit arithmetic by leaving 32
bit arithmetic. Here the question is put to the 32 bit type itself:

```rust
let last = addr.checked_add(len).ok_or(Error::Wrap)?;
let end = last.checked_next_multiple_of(line).ok_or(Error::Wrap)?;
```

**`checked_next_multiple_of` answers `None` exactly when the rounded end would
not fit in a `u32`**, which is finding four and its boundary in one call. The
case ending exactly at the top of the address space needs the value 2 to the
32nd; a `u32` has only zero to offer; and storing zero **is** the failure rather
than a near miss, because a loop from base to end then runs no times, with no
fault and no diagnostic.

The two forms agree on every oracle case and on the boundary. The difference is
not in the specification and is recorded because this is the fourth crate where
the answer to "what does this language add" is that **an arithmetic that cannot
be expressed is refused by the type rather than by a hand-written guard**:

| | What the type refuses |
|---|---|
| P11 | a backoff with no delay, where the C's sentinel is a legal delay |
| P13 | a drop count with no first reading, where the C needs a flag |
| P15 | a state outside the four, where the C needs a default arm |
| P19 | **an end address that does not fit, where the C computes in 64 bits** |

**And three widths now meet on one refusal**, which is worth more than two: the
C computes wider than the question, the Python has no width at all, and the Rust
asks the type. All three reach the same refusal on the same cases.

## One loop becomes a call

The C counts the region size's logarithm with a shift loop; this calls
`u32::ilog2`. Both answer the same number for every power of two, which is the
only input either reaches, since both refuse a size that is not one before
computing it.

P16's crate made the same kind of swap, `u64::isqrt` for a bit-by-bit descent,
and there it mattered: that was the one quantity in the volume where the four
implementations had real room to disagree, and writing a third one found a
saturation defect in the other two. Here there is no such room, because the
input is a power of two by the time the call happens. The difference between
those two swaps is the useful part.

## The version floor is 1.73 of the two calls and 1.75 of the workspace

`checked_next_multiple_of` and `ilog2` were both stabilised in 1.73, so this
crate could declare that. It declares 1.75, which is the workspace's floor and
`edge-llm-pi`'s scarthgap Rust, because a crate with a lower floor than the
workspace is a claim nothing checks. The two calls are named in `Cargo.toml` so
the floor does not look arbitrary.

## What is identical

The order of the six refusals and their names; the power-of-two test on the
line, because the rounding is a mask rather than a division; a buffer of no
bytes refused rather than answered with an empty range; the two shared lines
reported **separately**, which is finding one, so a caller can be refused for
the operation that cannot tolerate them; criterion 4 answered as one verdict
with a shared line in either position being a failure of alignment, which is the
criterion's own word; the region base checked against its **own** size rather
than against the cache line, which is finding three; and the absent upper bound
on the region size, which the C explains at length and this crate repeats: a
size above 2 to the 31st cannot be a power of two in a `u32`, so the test would
be a guard that can never fire, and a check that cannot fail reads as coverage.

`Error::Argument` is unreachable here, because a reference cannot be null and
the answers are returned by value. The name is kept so the six refusals mean the
same thing in all four implementations, and this sentence is the honest half of
that.

## The filter protocol

```text
M <addr> <len> <line>   -> base=.. end=.. bytes=.. lines=.. head=0|1 tail=0|1
B <addr> <len> <line>   -> ok=1
R <base> <size>         -> field=N
```

**Every value is decimal, addresses included.** Hexadecimal would read better to
a person and would add a second parser to every implementation for no gain,
which is the decision P01's filter already made for register words.

**`B` answers `ok=1` or a refusal and never `ok=0`.** Criterion 4 is a build
time assertion, so the only two outcomes are that it holds or that the buffer is
refused, and a filter printing a flag would invite a caller to read the flag
instead of the refusal.

**The line length travels in every range request**, because it is a parameter of
every function in all four implementations: two refusal cases pass a line of
zero and one of 48, so a filter with the measured 32 compiled in could not carry
them. A test counts how many distinct lines the requests ask for, so that
parameter cannot quietly become a constant.

A second test drives finding four's boundary through the filter in four
requests: an end of exactly 2 to the 32nd, a sum that wraps before rounding, a
rounding that crosses the boundary where the sum did not, and one line lower,
which must be answered.

## What is not here

The transfer engine, the stale read reproduced across a thousand runs, the three
fixes and their failure rates, the two cycle costs per transfer, the reachability
table against RM0455, and the fourth failure with its register evidence. Those
need the board, a sensor on a breadboard and RM0455. There is no measured number
in this crate; the one measured figure it quotes, the 32 byte line, came from
this project's own run on the board on Saturday 3 October 2026 and is named
rather than assumed.
