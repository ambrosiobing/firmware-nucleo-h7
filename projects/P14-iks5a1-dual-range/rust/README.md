# P14 in Rust

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `src/lib.rs` | **std**: the header parse, the scale lookup, the conversions, the relation, the drain time, nine unit tests |
| `src/main.rs` | six verbs on stdin, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies, and why this one is not `no_std` |

The three findings this project is about are in
[../c/recdecode.h](../c/recdecode.h) and are not repeated here: the event window
selects the scale with the worst altitude resolution, the altitude is
differential and the reference is not in the record, and step 10's figure omits
the overhead its own comment names.

## This crate is std, and the reason is one function

Step 9's relation raises a pressure ratio to a fractional power, and
**`f64::powf` is in std**: core carries the operators and the comparisons, and
the transcendental functions belong to the standard library. Writing a power
series here instead would produce a number that is not the chapter's relation
and not what the twin computes, and agreement with a twin is the whole point of
a second implementation.

So the volume's list now reads:

| | |
|---|---|
| P06, P18, P14 | `sqrt`, `log10` and `powf` are std, so those three crates are std |
| P12, P10 | `abs` is std, so both write the magnitude out and stay `no_std` |
| P07, P17, P13 | `+ - * /` and comparison are core, so `f64` alone needs no std |

The consequence is visible in CI rather than only here: the cross step that
compiles every `no_std` library for `thumbv7em-none-eabihf` does **not** name
this crate, and the guard that compares that step's list against every `no_std`
crate does not expect it to. Fifteen crates are named there; this is the
eighteenth crate in the workspace.

## The sensitivities are the caller's table and not constants here

That is the C's decision and the chapter's own reason one level further down:
the scale codes live in the record so that a record written under one
configuration can be read years later, and **a sensitivity compiled into a
decoder is exactly the constant that removes**. This bench holds neither
sensor's datasheet, so a table written into a crate would be an invention.

The oracle's table is powers of two, which makes every conversion exact in
binary and makes each finding about the **ratio** between two entries rather
than about either value.

**A code whose valid bit is clear is refused** rather than read out of the slot.
The slot holds zero, which would convert every count to zero milli-g and look
like a quiet channel: a decoder that answered that would be wrong in the one
way nothing downstream could detect.

## The header is parsed and not cast

`u32::from_le_bytes` and `u16::from_le_bytes`, so the little-endian contract is
in the call rather than in the platform. The chapter declares a packed structure
for the firmware to write, which is right there; casting a byte buffer to it on
a host would assume the host's byte order and its tolerance for unaligned
members, and **the whole value of a second implementation is that it shares no
assumption with the first**.

## What is identical

The field order and the byte order; the six refusals in the C's order and with
its names; every code refused rather than defaulted; the two flag bits that
cannot both be true, which the chapter never says are exclusive; a record with
no acceleration samples and a pressure count of zero without the blanked flag,
both refused as a header without a record; the altitude's scale factor and
exponent left exactly as the chapter prints them, 44330.0 and 0.1903; the
resolution taken **analytically** rather than by differencing two altitudes,
because a difference of two nearly equal altitudes is where the resolution of
that very calculation would appear as noise; and nine bit times a byte in the
drain time.

Two predicates are spelled differently and are the same comparison. The C
guards with `!(x > 0.0)`; clippy rejects that shape as
`neg_cmp_op_on_partial_ord`, so this writes `x.is_nan() || x <= 0.0`.

The bitwise tests carry parentheses the C also needs but for the opposite
reason: Rust binds `&` tighter than `!=` and C binds it looser, so
`(flags & KNOWN) != 0` is written out in both rather than leaving a reader of
four implementations to know which language they are in.

## The filter protocol

```text
H <n> <byte>*n                     -> tick=.. pre=.. post=.. baro=..
                                      lo=.. hi=.. range=.. flags=..
M <n> <byte>*n <table> <counts> <fine>   -> mg=..
P <n> <byte>*n <table> <counts>          -> hpa=..
A <p_hpa> <p0_hpa>                     -> m=..
R <lsb_per_hpa> <p_hpa> <p0_hpa>       -> m=..
D <samples> <bytes_per_sample> <overhead> <bus_hz>  -> ms=..
```

**The byte count travels with the bytes**, and one oracle case is the whole
reason: "a buffer shorter than the header" is **thirteen** bytes, and its point
is that the parse refuses before reading a field. This protocol carried exactly
fourteen until Thursday 29 October 2026, when skyhorizon answered BAD_REQUEST
for that case: the filter could not carry it, and **a filter that cannot carry a
case cannot test it**. That rule is written in P13's own filter, two projects
away, in the same words and about a different parameter, which is why it is
recorded here as a recurrence rather than as news.

**The table travels in every request that needs it**, fifteen tokens, and the
filter holds nothing between lines. That is deliberate rather than frugal: a
filter that remembered a table set by an earlier request would answer a request
that never named one, and what this project tests is precisely that a decoder
uses the record's own codes **against a table it was given**.

The conversions are compared exactly, because the table is powers of two. The
altitudes raise a ratio to a fractional power, which is a library call in every
language, so those are compared to a relative 1e-12 and nothing tighter, which
is what the oracle's own note says.

A test builds every request with no filter and no compiler, reads each field
back and compares it with the case, and a second drives finding one through the
filter: the wide range's resolution over the narrow range's is exactly the ratio
of the two sensitivities, asserted rather than described.

## What is not here

Both acceleration channels live, the watermark line, the pre-trigger ring, the
arming on the high-g line, the cost of changing the barometer's full scale, the
foam-pad event, the sealed bag, and the rate at which the bus stops keeping up.
Those need the X-NUCLEO-IKS5A1 on the Arduino header, which the bench holds, and
the I2C pin mapping that [../README.md](../README.md) describes. There is no
measured number in this crate.
