# P20 in Rust

**State: host, since Wednesday 28 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI, which runs `cargo fmt --all --check`, clippy with
warnings as errors, and the tests.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`: the histogram, the percentile ranks, the three-way verdict, the stack arithmetic, six unit tests |
| `src/main.rs` | four verbs on stdin, so one parity test drives this beside the C and the Python |
| `Cargo.toml` | no dependencies, and why this crate is `no_std` where two others are not |

The five findings this project exists for are written out in
[../c/latstat.h](../c/latstat.h) and are not repeated here. One of them behaves
differently in this language, and that is what this page is about.

## Finding four is the one the language changes

The 99.9th percentile's rank is the ceiling of 999 times the count over 1000.
The counter is 32 bits and holds four thousand million, where a 32-bit product
of 999 and the count wraps:

| | 999 times a count of four thousand million, in 32 bits |
|---|---|
| C, every build | **1680414720**, silently |
| Rust, debug or test | **panics**, "attempt to multiply with overflow" |
| Rust, this workspace's release profile | 1680414720, silently |

Cargo's dev profile takes `overflow-checks` from `debug-assertions`, which is
on, and the root manifest's `[profile.release]` sets `opt-level`, `lto`,
`codegen-units`, `panic` and `debug` and **not** `overflow-checks`, so release
leaves it off.

**So the honest statement is that Rust's arithmetic checks would have caught
this during development and not in the field.** That is smaller than "this
cannot happen in Rust" and it is the only claim the two profiles support.

**This is the second project in the volume to find that exact shape**, after
P17's subtraction on Tuesday 27 October 2026, where the same three rows appear
for `(b - a) - overhead`. One instance was an anecdote about a profile; two make
it a property of the toolchain worth stating once and referring to afterwards.

And the fix was available in every language: the product is 64-bit in all three
implementations here, which is not a language feature but an arithmetic choice
the C made as well.

A test shows it through `wrapping_mul` rather than a plain multiply, because a
plain one panics under test and wraps in release, and the point is what the C
does in every build. The same test then shows the two ranks landing in different
bins of one histogram, 50 against 900, **which is what makes the wrap a wrong
answer rather than a wrong intermediate**: bin 50 passes the 250 microsecond
budget row that bin 900 fails.

## What else the language changes, which is almost nothing

`Error::Argument` is the C's null pointer refusal and **is unreachable here**,
because a reference cannot be null. It is kept so that the six refusal names
mean the same thing in all three implementations and a filter answer never has
to be translated, and the only thing a test can assert about it is its name.

`Result` carries a refusal instead of a negative return code. The histogram is
an array in a struct, exactly as it is in the C. `Verdict` is an enum with a
`code` method returning the C's 0, 1 and 2, so one oracle field serves every
language.

## What is identical

The bin width of one microsecond and the 1024 bins of step 5; the truncating
division that turns cycles into microseconds; the pinned rank definition, the
sample of rank ceil(p times n) counting from one; the count checked against the
bins plus the overflow rather than trusted; an unresolved percentile reported as
a flag rather than as the top bin; the three-way verdict, where a pass needs the
whole bin below the budget and a failure needs the whole bin above it; the
eighty percent stack rule as a comparison rather than a division; and the six
refusals with their names.

## The filter protocol

Four verbs, counts before data, samples run-length encoded because the chapter's
run is a million releases.

```text
R <cycles_per_us> <npairs> <cycles repeat>*npairs    record a run, report, judge
B <over> <max_us> <count> <npairs> <bin count>*npairs  a histogram built directly
D <count_before> <cycles> <cycles_per_us>            one sample onto a preset counter
S <budget> <free>                                    one task's stack
```

`R` and `B` answer with the report and the verdict on one line, judged against
table 20.4, which the library carries as constants. `D` answers `ok` or a
refusal, which is how the oracle's counter-at-maximum case is reached without
taking four thousand million samples. `S` answers `used`, `permil` and `within`.
A refusal is `REFUSED` and one of `argument`, `rate`, `full`, `empty`, `count`
or `stack`, and a request with anything left over is refused.

## What is not here

Not the board, and not FreeRTOS. This crate is `no_std` and builds for the host
only, because `code.yml`'s cross step names its no_std crates one by one and
that step is a workflow change, which only the runner can answer. `p16-winfeat`
and `p17-tbmath` are in the same position. There is no measured number in this
project: the budgets are quoted from table 20.4, which was written before any
measurement, and this is the arithmetic that will judge the figures when they
arrive.
