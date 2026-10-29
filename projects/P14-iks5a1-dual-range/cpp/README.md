# P14 in C++

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which compiles nothing by rule; the first compiler to see it is gcc 15 in WSL on
the win11 skyhorizon demo laptop, bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `recdecode.hpp` | header only: the header parse, the scale lookup, the conversions, the relation, the drain time |
| `recdecode_filter.cpp` | six verbs on stdin, so one parity test drives this beside the Rust |

The three findings this project is about are in
[../c/recdecode.h](../c/recdecode.h) and are not repeated here.

## What C++ adds, and the more interesting entry is what it does not decide

| | What C++ adds |
|---|---|
| P18 | one function template where the C needs two forty line copies |
| P12 | the C's lack of an allocator costs it sixty lines of fixed buffers, bounds checks and an insertion sort |
| P17 | almost nothing: four small functions over fixed-width integers |
| P16 | a fourth independent algorithm for the one quantity where implementations have real room to disagree |
| P20 | `std::optional` turns finding three from a convention into a type |
| P04 | `std::variant` reaches the same guarantee the Rust gets from an enum |
| P07 | an allocator, again: a parameter pair and a refusal disappear |
| P10 | `std::variant`, again, on a promise the C's own header makes in prose |
| P11 | two C conventions become types: a sentinel return and a sentinel terminated array |
| P13 | `std::optional` and a sized container, both of P11's answers at once |
| P14 | **the same two again, and no decision about the standard library** |

**The part worth writing down.** The Rust crate beside this file had to declare
itself `std` rather than `no_std`, because step 9's relation raises a ratio to a
fractional power and `f64::powf` lives in std: that language splits its library
into a core a bare-metal target can always have and a std it cannot. **C++ has
no such split in this volume**, because `<cmath>` is simply there, `std::pow`
costs nothing to reach, and the question of whether this file could link on the
board never arises, since every C++ variant in this repository is a host filter
driven by the tests.

**That is not C++ winning.** It is the same question asked where it has no
answer. The C++ freestanding implementation has its own rules about what is
available, and nothing here exercises them. The Rust's declaration is a checked
fact about a board target; this file's silence is the absence of a claim, and
the two are not comparable. Recorded so the list does not read as a score.

## The two it does add are P13's

`std::optional<Error>` for the six refusals, so a refusal is a different type
rather than a negative return code. And a `const std::vector<std::uint8_t> &`
for the record bytes, so **a buffer shorter than the header is a refusal** where
the C's pointer and length pair leaves it to the caller. The oracle has exactly
that case, thirteen bytes, and its point is that the parse refuses before
reading a field.

## What is identical

The field order and the little-endian byte order, parsed byte by byte rather
than cast; the six refusals in the C's order and with its names; every code
refused rather than defaulted, including a code the format can express and the
caller's table does not define; the two flag bits that cannot both be true,
which the chapter never says are exclusive; the altitude's scale factor and
exponent exactly as the chapter prints them, 44330.0 and 0.1903; the resolution
taken **analytically** rather than by differencing two altitudes; and nine bit
times a byte in the drain time.

Three lines are written for the warning set rather than for the chapter, and all
three were found by reading the flags, since no compiler runs on the authoring
laptop: the two code bounds widen the byte explicitly before comparing it with a
`std::size_t`, the complement of the known-flags mask is cast back to
`std::uint8_t`, and `le16` binds its two bytes to named `std::uint32_t` locals
rather than nesting four casts into one expression that ran past eighty columns.

One function in the C has no counterpart here: the C's `rec_drain_ms` takes no
decode cost, so the `below_zero` helper that P13 needs is absent, and it was
deleted rather than left as dead code once that was noticed.

## And it needs a build flag the Rust does not

`-ffp-contract=off`, because the drain time is a multiply followed by an add,
the shape gcc fuses at `-O2`. **The oracle compares these figures to a relative
1e-12 rather than exactly, so a fused result would probably pass**, and probably
passing is not a reason to let a build differ from its twins.

## The filter protocol

```text
H <n> <byte>*n                     -> tick=.. pre=.. post=.. baro=..
                                      lo=.. hi=.. range=.. flags=..
M <n> <byte>*n <table> <counts> <fine>   -> mg=..
P <n> <byte>*n <table> <counts>          -> hpa=..
A <p_hpa> <p0_hpa>                       -> m=..
R <lsb_per_hpa> <p_hpa> <p0_hpa>         -> m=..
D <samples> <bytes_per_sample> <overhead> <bus_hz>  -> ms=..
```

**The byte count travels with the bytes**, and this file carried it from the
first line it was written, because the Rust's protocol had already been caught
without it on skyhorizon earlier the same day. **The table is fifteen tokens and
travels in every request that needs it**, with nothing held between lines.

One test function drives both filters for each of the two things they are asked:
every case in the oracle's order, and finding one, where the wide range's
resolution over the narrow range's must be exactly the ratio of the two
sensitivities.

## What is not here

Both acceleration channels live, the watermark line, the pre-trigger ring, the
arming on the high-g line, the cost of changing the barometer's full scale, the
foam-pad event, the sealed bag, and the rate at which the bus stops keeping up.
Those need the X-NUCLEO-IKS5A1 on the Arduino header, which the bench holds, and
the I2C pin mapping that [../README.md](../README.md) describes. There is no
measured number in this file.
