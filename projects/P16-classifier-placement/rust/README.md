# P16 in Rust

**State: host, since Tuesday 27 October 2026.** Written on win11 aquamarine,
which has no `cargo` and will not get one; the first toolchain to see it is the
one `rust-toolchain.toml` pins, in WSL on the win11 skyhorizon demo laptop,
bing@JPTOUPM678, and then CI, which runs `cargo fmt --all --check`, clippy with
warnings as errors, and the tests.

| File | What it is |
|---|---|
| `src/lib.rs` | `no_std`: the three pinned feature groups, the bounded tree walk, seven unit tests |
| `src/main.rs` | two verbs on stdin, so one parity test drives this beside the C and the Python |
| `Cargo.toml` | no dependencies, and why this crate alone declares 1.84 |

## Why a third algorithm, rather than a third transcription

Chapter 16's criterion 1 is an equivalence gate: identical features and identical
classes, with the build failing if they differ. The chapter's three
implementations are the three placements, in the sensor, on the microcontroller
and on the host; only the host one exists, so what the gate compares today is
that one written in three languages. **A gate like that is worth running in
proportion to how independently the implementations were written.**
Two transcriptions of one algorithm mostly prove that the transcription was
faithful.

The floored integer square root is the one place in this project with real room
to disagree, so the three take three routes:

| | The floored integer square root |
|---|---|
| C | a bit-by-bit descent, written out, because a floating-point `sqrt` and a cast would agree almost always, and almost always is what an exact gate exists to refuse |
| Python | `math.isqrt` |
| Rust | `u64::isqrt`, the standard library's, exact by contract |

**That costs this crate a declared minimum Rust version of 1.84 where the other
ten in this workspace declare 1.75**, because `isqrt` stabilised there. The
alternative was to hand-roll a fourth descent, which would have made this a
transcription of the C and would have put an unverified algorithm into the
function the gate most depends on. The pinned toolchain is 1.99.0, so nothing is
blocked by the choice. Nothing in CI checks the declared minimum, so this is a
statement to a reader and not a constraint on the build, and it is written here
rather than left to be noticed.

## What writing it found, which is the whole argument for the gate

**The root mean square did not saturate, and peak to peak did.** Both are Q15,
both can exceed what Q15 holds, and the C header's own sentence for peak to peak
is that a wrapped value "would read as a quiet window, which is the most
misleading answer available here". The same sentence applies to the root mean
square, and it had not been applied.

On a window with every sample at negative full scale the mean of the squares is
1073741824 and its root is exactly 32768, which Q15 stops one short of:

| | The root mean square of a window of negative full scale |
|---|---|
| C, before Tuesday 27 October 2026 | negative 32768, from the cast to `int16_t` |
| Python, before Tuesday 27 October 2026 | 32768, which is not a Q15 value at all |
| all three, now | 32767, saturated, as peak to peak already was |

The two had never been compared there, because **no oracle case reached the
corner**. The oracle now carries that window, each implementation saturates, and
a unit test here states the unsaturated root as well as the saturated one so the
test shows what was saturated rather than only the result.

That is the gate paying for itself: the corner where two implementations
disagreed was found by writing the third, not by reading either.

## What is identical, and what is not

Identical: the window length of 128 samples and three axes, refused rather than
scaled; the mean as an arithmetic shift of seven, truncating toward negative
infinity; the mean of the squares floored before its root is taken; the
saturation of the root mean square and of peak to peak at 32767; the six
refusals and their names; the depth bound on the tree walk; and at or below the
threshold going left.

Not identical, by language: a refusal is an `Error` in a `Result` here and a
negative return code in the C, and the names rather than the codes are what the
filter protocol carries, so a code never has to mean the same number twice.
`copy_from_slice` builds the flat vector where the C writes three indexed
assignments.

**The band energies of step 3 are absent here too**, for the reason
[../c/winfeat.h](../c/winfeat.h) gives: a quantity named as coming from an
unspecified transform cannot enter an exact gate.

## The filter protocol

Two verbs, each taking its counts before its data, so the parser never guesses
where a list ends. A node is five values in the header's order.

```text
F <n> <axes> <n*axes samples>
    -> mean=a,b,c rms=a,b,c p2p=a,b,c vector=v0,..,v8
C <nodes> <depth_max> <nfeat> <feature leaf threshold left right, per node> <nfeat features>
    -> class=k
```

A refusal is `REFUSED <name>`, one of `argument`, `shape`, `tree`, `node`,
`feature` or `depth`. A request with anything left over is refused, which caught
a real defect in P18's test on Monday 26 October 2026: ignoring a remainder lets
a caller send one thing and be answered about another. One window is 384
integers, so an `F` request is 386 tokens and the filter reads whole lines rather
than a fixed buffer, after P08's filter split a twenty-one-thousand-character
request into several and answered twenty requests with twenty five answers.

## What is not here

Not the board. This crate is `no_std` and builds for the host only, because
`code.yml`'s cross step names its no_std crates one by one and this crate is not
among them. The same is true of `p17-tbmath`, written the same day, and of
`p20-latstat` and `p04-rxspan` since.

**That sentence used to end "which is answered by the runner and not by this
laptop", and on Thursday 29 October 2026 that turned out to be two claims rather
than one.** The runner answers for its image and its toolchain. The step's
SCRIPT is a cargo command, and the target it names is in
`rust-toolchain.toml`, which the laptop with a compiler also installs, so the
script can be run there: `python3 python/tools/run_workflow_step.py` exists for
exactly that, and it was written after a literal backslash-n in a workflow
script cost a red commit and three days of a wrong diagnosis. Neither has a measured
number on the board, and the project README says what that waits on: the IKS4A1
shield, a Raspberry Pi and the PPK2.
