# P11 in Rust

**State: host, since Thursday 29 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`: the classifier, the wrap-safe deadline test, the backoff, nine unit tests |
| `src/main.rs` | three verbs on stdin, so the parity test drives it beside the C and the Python |
| `Cargo.toml` | no dependencies, and why the library is `no_std` |

The reasoning this project is built on is in
[../c/atparse.h](../c/atparse.h) and is not repeated here, including the one
that decides the project: chapter 11's deadline test is a signed difference on a
32 bit counter, the obvious form is correct for forty-nine days and then is not,
and **a board cannot demonstrate that without waiting forty-nine days**. A host
sets the counter to whatever it likes, which is the whole argument for writing
this half of the chapter before the modem arrived.

## The subtraction is `wrapping_sub`, and that is the fourth instance

`cargo test` builds the dev profile, which takes `overflow-checks` from
`debug-assertions`, so `now_ms - deadline_ms` panics whenever now is before the
deadline. **That is not an edge case here: it is every case where the deadline
has not passed**, which is three of the oracle's nine.

This is the fourth time this workspace has met that check and the first time the
wrap is the entire subject of the function rather than a consequence of it. The
volume's list:

| | |
|---|---|
| P17 | the elapsed subtraction, a real defect the check found |
| P20 | the rank product, a real defect the check found |
| P04 | the advance, correct and intended, written `wrapping_sub` for this reason |
| P11 | the deadline test, where the wrap **is** the specification |

## The backoff returns `Option<u32>` where the C returns a sentinel

`AT_BACKOFF_REFUSED` is `0xFFFFFFFFu`, which is **a legal value of the return
type**: a caller that forgets to compare gets a delay of about forty-nine days
rather than an error. `None` cannot be used as a delay.

The refusal is the same refusal in both, for the same three reasons: a base of
zero, a cap below the base, and a cap above half the range, where the doubling
would overflow and come back as a plausible small number. Three of the oracle's
ten backoff cases are refusals and the parity test counts them rather than
trusting that they appeared.

`Option` rather than the `Result` the other crates return, and the difference is
real rather than a preference: those carry several named refusals and this one
carries three conditions that produce one outcome, no delay, which the C spells
with a single sentinel too. The rule behind both shapes is the same, and it is
the one this volume paid two red commits for: **a refusal must be expressible by
every signature**, so the signature changes rather than the case being exempted.

## What is identical

The two tables and their contents; the prefix match rather than an equality
test, which is what lets `+CME ERROR: 10` and `CONNECT 115200` classify
correctly and what makes `OKAY` a success; the order the three classes are
tried in, so a line matching both a final code and an unsolicited prefix is
final; the empty line as its own answer; the signed reading of the difference
and its real bound of about twenty-four days; the doubling that stops at the
cap; the scatter applied **after** the cap, so the answer can exceed it by up to
an eighth; and the span falling back to one where an interval is too small to
take a quarter from.

Not identical, and forced by the languages rather than chosen. The C takes
`const char *const *urcs` and reads `NULL` as "no table yet", which its header
says is not an error. **A slice cannot be null**, so an empty slice says it
here, and the behaviour is the one the C gives for a null table and for a table
of no entries alike: every line that would have been unsolicited is
intermediate. A test drives the whole oracle through the filter with no table
and checks that exactly the unsolicited lines move and nothing else does.

## The filter protocol

```text
C <n_urcs> <urc field>*n <line field>   -> class=final_ok | final_bad | urc
                                                 | intermediate | empty
D <now_ms> <deadline_ms>                -> due=1 | due=0
B <attempt> <base_ms> <cap_ms> <rand>   -> delay=NNN | REFUSED backoff
```

**A field is a length and then hex, and the oracle is the reason rather than a
taste for hex.** Nine of the seventeen classify cases contain a space, four
contain a comma or a double quote, `+CMTI: "SM",3` among them, and one is the
**empty** line, which is a class of its own here. **Ten of the seventeen cannot
cross a whitespace-separated protocol at all**, and a filter that cannot carry a
case cannot test it. The length travels with the hex
because a whitespace split cannot produce an empty token, and a hex token whose
decoded length disagrees with the stated one is refused rather than classified
as the shorter line.

**The refusal crosses as a word and not as `0xFFFFFFFF`,** for the same reason
the function returns `Option`: carried as a number it would be
indistinguishable from an answer.

A test builds every request with no filter and no compiler, **reads each line
back out of its hex and compares it with the case**, and it is shown to catch a
wrong request rather than only a malformed one: a builder that drops the last
byte of every line is refused on 16 of the 17 cases, the seventeenth being the
empty line, which has no last byte, and a builder that sends the prefixes of a
table it claims is absent is refused on all 17.

## What is not here

The engine itself, its queue, the completion callback that must fire exactly
once in every outcome, the unsolicited handler table and the hundred injected
faults. Those need the module, the byte path of chapter 4 and the framing of
chapter 5. There is no measured number in this crate.

It does compile for the board's target: the guard of Thursday 29 October 2026
compares the cross step's crate list against every `no_std` crate, so the suite
turns red on the authoring laptop the moment a crate is absent from that step.
This one was absent for minutes.
