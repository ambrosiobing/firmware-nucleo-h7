# P13 in Rust

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std` with `f64`: the decode, the gap, the budget, six unit tests |
| `src/main.rs` | three verbs on stdin, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies, and why the library is `no_std` |

The four findings this project is about are in
[../c/fifodecode.h](../c/fifodecode.h) and are not repeated here: the chapter's
drop count is computed in the wrong modulus, it fabricates drops at startup, a
gap of exactly the counter's modulus is invisible, and the fill time divides by
one source's rate while the buffer holds several.

## Finding two becomes a type, and that is a first for this volume

The C's carried state holds **two fields for one fact**:

```c
uint8_t expect_cnt;  /* carried between drains */
uint8_t started;     /* 0 until the first word is seen */
```

Finding two is that the chapter has no `started` at all: its `expect_cnt` is a
static beginning at zero, so the first word of the first drain is measured
against a zero nobody wrote, and the logger opens by reporting drops that never
happened. The C's answer is to add the flag, and that is a correct fix.

**But the pair can disagree.** `started` set with no expectation, and an
expectation while `started` is clear, are both representable, and nothing in the
type stops a later edit producing either. Here it is one field:

```rust
pub expect: Option<u8>,
```

`None` **is** "no word has been seen yet", so the two states cannot come apart,
and the comparison against an expectation cannot be written without first taking
the expectation out of the option.

Four crates in this volume now replace a convention with a type, and this is the
first where the thing carried is a **finding** rather than a refusal:

| | What the type carries |
|---|---|
| P20 | a percentile no bin resolves, which must not read as the fastest run |
| P11 | a refused backoff, where the C's sentinel is a legal delay |
| P10 | a refusal that carries no ledger, which the C's header promises in prose |
| P13 | **a drop count with no first reading yet**, which is finding two itself |

## The slice carries its own length

The C takes `const uint8_t *buf` with a separate `words`, and its header says
"words is the buffer level already clamped by the caller", so a `words` larger
than the buffer reads past the end: **undefined behaviour rather than a wrong
answer**. Here a buffer too short for the stated word count is
`Error::Argument`.

**No oracle case reaches that**, because the oracle's buffers are built to
match, so this is not a new case the C would have to answer. It is the same
refusal name used for a condition the C cannot detect, which is the honest way
to add a check without changing the specification. A unit test exercises it.

## `no_std` with `f64`, and the running list of what that costs

The decode is integer arithmetic and the budget is addition, multiplication,
division and comparison, **all of which are in core**. So this crate is
`no_std` and the board can link it.

| | |
|---|---|
| P06, P18 | `sqrt` and `log10` are std, so those two crates are std |
| P12, P10 | `abs` is std, so both write the magnitude out and stay `no_std` |
| P07, P17, P13 | `+ - * /` and comparison are core, so `f64` alone needs no std |

One predicate is spelled differently and is the same comparison. The C guards a
rate with `!(x > 0.0)`; clippy rejects that shape as
`neg_cmp_op_on_partial_ord`, so this writes `x.is_nan() || x <= 0.0`, which
refuses the same values. The decode cost has its own form, `x.is_nan() || x <
0.0`, because zero is allowed there and the oracle has a case for a negative
one.

The counter subtraction is `wrapping_sub`, which is the **fifth** instance in
this workspace of the dev profile's overflow checks deciding a line: `cnt` below
`expect` is precisely the wrap `cnt_gap` exists to measure, so `cargo test`
would panic on it rather than answer.

## What is identical

The gap taken in the counter's own modulus; the order of the five refusals and
their names; the tag table bound and an unknown tag counted rather than indexed;
the first word establishing the sequence; the counter advanced as `(cnt + 1)`
masked, so a wrap in the expectation is the same wrap the C has; the buffer
filling at the **sum** of the batching rates with the chapter's one-rate form
reported beside it; the bus model of nine bits a byte plus two address bytes
plus a start and a stop; and the undetectable gap reported as `1 << cnt_bits`.

## The filter protocol

```text
G <expect> <cnt> <cnt_bits>              -> gap=N
D <word_bytes> <tag_shift> <cnt_shift> <cnt_bits> <known_tags>
  <words> <n> <first byte>*n             -> words=.. unknown=.. drops=..
                                            gaps=.. tags=a,b,..  (32 counts)
B <watermark> <sources> <word_bytes> <cnt_bits> <bus_hz> <decode_us>
  <rate>*8                               -> entries=.. fill=.. chapter=..
                                            drain=.. ratio=.. gap=N ok=0|1
```

**The word count and the number of supplied bytes are separate tokens**, and one
oracle case is the whole reason: "more words than the landing buffer can hold"
passes a level of 1025 with no bytes at all, because that refusal is decided
before anything is read. A protocol that derived the level from the bytes could
not carry that case, and a filter that cannot carry a case cannot test it. The
request test counts that case rather than trusting it is there.

**The filter pads the block itself**, which is what the C's parity test does on
its side: one tag byte per word followed by `word_bytes - 1` zeros. No part of
this arithmetic reads the data bytes, and sending them would make a request for
a 1024-word drain seven thousand tokens long.

**All thirty-two tag counts come back**, zeros included, as one comma separated
token, so the test asserts the empty slots as well as the counts the oracle
states.

The budget figures are compared to a relative 1e-12, this project's own stated
tolerance, and the undetectable gap and the verdict flag are integers and are
compared exactly.

## What is not here

The bus shim, the identification registers, the batching and watermark
configuration, the interrupt route, the empty handler, the overrun flag, the
ten-minute run and the comparison against polling. Those need the
X-NUCLEO-IKS4A1 on the board and the I2C pin mapping that
[../README.md](../README.md) describes. There is no measured number in this
crate: the decode cost per word that the budget takes as a parameter is exactly
what step 8 says to measure with the cycle counter rather than assume, and it
has not been measured.
