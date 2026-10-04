# Rust on this part

**Seven crates**, P09's codec, P05's framing, P08's state machine, P02's ring,
P03's loss attribution and P06's rate witness since Saturday 3 October 2026, and
P12's two gates since Sunday 4 October 2026, all in the workspace at the
repository root. All six are proven against their projects'
own oracles as of Sunday 4 October 2026, with 38 properties in `cargo test` that
need no other implementation.

**Six of the seven are `no_std`, and which one is not is the finding.** `p06-rate` is
`std`, because `f64::sqrt`, `f64::abs` and `f64::is_nan` live in `std` and not in
`core`: Rust's core library has no floating-point maths at all, since those
functions live in the platform's libm and `core` assumes no platform. A `no_std`
version would need the `libm` crate.

The C is different and that is the part worth printing. `rate.c` compiles
unchanged for the target, because newlib provides `sqrt`. The same program is
portable to this part in C and is not in Rust without a crate, which is a cost of
the language in the one direction people do not usually expect. `code.yml`
therefore names the five `no_std` packages for the `thumbv7em-none-eabihf` build
rather than building the workspace, and says why at the step.

**P12's crate turns P06's finding from an anecdote into a pattern.** It came
within one function call of the same fate: the charge gate reconciles a reported
total against the sum of its phases, and the obvious spelling of that needs
`f64::abs`, which is in `std` for exactly the same reason `sqrt` is. Written as
two comparisons instead it needs nothing, so the crate is `no_std` and the
`thumbv7em-none-eabihf` step includes it. Two crates, the same obstacle, two
different answers: `core` has no floating-point maths at all, that bites any
numeric code written for a target in Rust, and whether it costs a dependency
comes down to whether the particular arithmetic can be spelled without libm.
P06's could not and P12's could, barely.

It does need `alloc`, because its answer is a string of unknown length, and that
is the other half of the comparison. The C avoids allocation with fixed buffers
and a refusal when they do not fit, which is the right shape for a file that
compiles for the target and costs it about sixty lines of appending, bounds
checking and an insertion sort that the Rust and the C++ do not have. A `--lib`
build for the target needs no allocator, which is why CI can include the crate;
a binary would, and nothing here has one.

**P06's carries a finding of a different kind**, and it is about a tool rather
than the language. `cargo clippy -D warnings` refused `p06-rate` alone of the six
crates, on a comparison against a budget constant whose value is the minimum of
its type, and its suggested replacement would have inverted the criterion the
moment the budget stopped being zero. The lint is right about the code as it
stands and wrong about the code as it is meant to be read, so it is silenced by
name at that one statement with the reason beside it. The C, C++ and Python write
the same comparison and have no equivalent lint to answer, so this is a defect
class only the Rust implementation could have raised, and the answer to it was a
written argument rather than a code change.

**P03's carries the best argument for the language in the volume so far**, and it
is not about memory safety. Its `Verdict` is an enum of five variants and `match`
on it is exhaustive, so adding a sixth kind of loss will not compile until every
place that decides what a verdict means has been visited. In the C a sixth
enumerator compiles everywhere and falls through whatever `else` was written
last. For a file whose entire purpose is that two kinds of loss are never
confused, that is the property worth having. P08's is the
first to depend on another: it takes `p09-payload` as a path dependency, because
its acceptance criterion is that the transmit stub's bytes equal what P09's
encoder produces, and a dependency makes that true by construction where a second
copy would make it true by agreement. This page carries the research that
established Rust is a genuine option on this exact device rather than an
aspiration, which is worth not losing now that there is code to go with it.

## What was verified for this part, not for the family name

The checks below were made against feature flags, board definitions and target
manifests naming this exact device, not the STM32H7 family. That distinction is
the whole point: most material that says "STM32H7" means the STM32H743, which is
a different part with a different reference manual.

| Piece | State for the STM32H7A3 |
|---|---|
| Peripheral access crate | has a module for this part |
| Hardware abstraction crate | has a feature flag for it, routed through its twin with the right manual's pin mapping |
| Async framework | names this exact package and pin count |
| Flashing and debugging tool | knows the exact die on this board, `STM32H7A3ZITxQ` |
| Real-time framework | works, because it schedules on the interrupt controller, which is architecture level rather than part level |

**The one gap, and it is an opportunity.** The async framework has no worked
example for this part, only for its twin. That is a contribution this volume
could make rather than a reason to avoid it.

## Why the toolchain question is different here

The C side of this repository is blocked on three downloads: `arm-none-eabi-gcc`,
a flashing tool, and `ninja`. Rust needs none of them. A target added to the Rust
toolchain plus the flashing tool is the whole requirement, and that flashing tool
is the same one the C side wants, so installing it unblocks both.

## What belongs here when it starts

**This changed on Saturday 3 October 2026.** Rust is no longer assigned to a few
projects by the variant matrix: every project carries a `rust/` directory
alongside its `c/`, `cpp/` and `python/`, and the language is meant to be built
for each of the twenty and proven equivalent to the other three against that
project's own oracle. What lives at this top level is only what more than one
project uses, the same rule the `c/` tree follows.

**The first crate is P09's**, in `../projects/P09-payload-codec/rust/`: a `no_std`
codec driven against the same hand-computed vectors, with `panic = "abort"` and
`opt-level = "s"`. It was first because everything it needed already existed:
six vectors, three implementations to disagree with, and a board image that
prints the same bytes. Written and proven on the host the same day, Saturday
3 October 2026, with an hour in between during which it carried the fifth state
this volume now names rather than glosses: **written**, and not yet built.

**The workspace arrived with that crate and not before.** A root `Cargo.toml`
listing no members, and a `rust-toolchain.toml` pinning a channel for nothing,
would have been two files that cannot be built, which is the same defect as an
empty `main.rs`. Both were written on Saturday 3 October 2026, the day the first
crate was.

**The second crate is P05's**, `p05-frame`, written and proven the same day:
COBS, CRC-16 and the frame, with the published check value 0x29B1 asserted by a
`const` assertion so a crate whose parameters are wrong does not build. Its
`cargo test` includes the exhaustive walk of all 64 single-bit flips of one
seven-byte frame, which needs no random seed at all.

`../Cargo.toml` lists members explicitly rather than globbing `projects/*/rust`,
because a glob would match the nineteen directories that hold only a README and
fail, and because the list is the thing a reader wants to see.
`../rust-toolchain.toml` pins 1.99.0, the `thumbv7em-none-eabihf` target and the
three components CI runs, for the same reason the C side prints
`arm-none-eabi-gcc -dumpversion` into its artifact: a figure from an unnamed
compiler is not a measurement anybody can repeat.

**P02's crate carries the sharpest Rust finding so far**, and it is not a
favourable one. The C's ring is one struct holding both indices, handed to a
producer in thread mode and a consumer in an interrupt handler, and that shape
does not compile in safe Rust: two contexts holding `&mut` to one value is what
the borrow checker exists to refuse. The C's version is sound only by a written
argument the compiler cannot see. So the choice is to encode that argument in a
type, with a producer and a consumer handle over one buffer, or to opt out of the
checking with `unsafe`. The crate takes neither and says so: its methods take
`&mut self`, which is a correct sequential ring and a fair host comparison, and
the two-context split is named as missing rather than faked.

P17 keeps Rust as its *subject* rather than its implementation language: the same
driver written at the vendor layer and at the register layer, with the Rust
version as the third reading.

A project here follows the same rule as everywhere else in this repository.
Nothing claims a measurement it has not taken, a value that is not confirmed
against RM0455 makes the code refuse rather than guess, and a figure that has not
been measured says "not measured".

## Why there is a folder per language at all

A reader opening this repository should see immediately what it is written in,
and a reader opening one project should see its four languages side by side
rather than having to visit four trees. So the four folders exist twice over: at
the top level for shared code, and inside each of the twenty projects for that
project's own.

That makes the Language axis of the volume navigable in the tree rather than only
in an appendix table. It also keeps one distinction visible that the old layout
blurred. Python is two different things here: a deliverable in its own right for
P09's codec and P12's gates, and a harness that drives C for everything else. The
first kind sits in a project's `python/`; the second sits in `../python/tests/`
and `../python/tools/` and is not a variant of anything.

Whether a toolchain exists for a language is a separate question from whether
the directory does, and each project's language README answers it for that
project. On this part, two of the four reach the board through a compiler, one
reaches it as a downloaded interpreter, and one of the four cannot make the
volume's timing claims at all.
