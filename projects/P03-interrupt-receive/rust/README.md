# P03 in Rust

**State: host only, proven Saturday 3 October 2026** in WSL on the win11
skyhorizon demo laptop, bing@JPTOUPM678, with rustc 1.99.0, and then in CI at
commit 92b0bf7. What that run established:

| Step | Result |
|---|---|
| `cargo fmt --all` | reformatted one block, committed as its own change |
| `cargo clippy --workspace --all-targets -- -D warnings` | clean on the first run |
| `cargo test --workspace` | 9 passed, the most of the five crates |
| `cargo build --release --workspace --lib --target thumbv7em-none-eabihf` | compiles, which is the proof the crate is genuinely `no_std` |
| `python3 -m pytest python/tests/test_attribute_parity.py -q -s` | all four agree over 19 steps and four ramps |

| File | What it is |
|---|---|
| `Cargo.toml` | the crate `p03-attribute`, no dependencies, a workspace member |
| `src/lib.rs` | the attribution, `no_std` except under `cargo test` |
| `src/main.rs` | the filter `p03-filter`, the three verbs the C++ filter speaks |

## What Rust adds, and here it is the most of the four

Two things, and both change the shape rather than the wording.

The refusal is a `Result`, so a caller cannot read a row that was never produced.
The C returns `-1` and fills nothing; the C++ returns `Option` and is as safe as
this.

The second is the one the C cannot have at all. `Verdict` is an enum of five
variants and `match` on it is exhaustive, so adding a sixth kind of loss later
will not compile until every place that decides what a verdict means has been
visited. In the C a sixth enumerator compiles everywhere and falls through
whatever `else` was written last. For a file whose entire purpose is that two
kinds of loss are never confused, that is the property worth having, and it is a
better argument for the language than anything in P05 or P08.

`cargo test` carries eight properties that need no other implementation,
including the three-counter overflow refused rather than wrapped, and the
unsorted ramp where the lowest rate must be found rather than the first row.

## What it is proven against

Synthetic steps whose answers are known by construction, nineteen of them, driven
through every implementation by `python/tests/test_attribute_parity.py` and
compared against the C. The clean cases are not the interesting ones:

| Step | Why it is in the list |
|---|---|
| the bridge case | a tenth of the traffic never reached the peripheral and the target lost nothing. Reporting it as target loss would be the wrong conclusion, and this is why the file exists |
| the bridge and the target both lost bytes | the case the verdict **order** protects. BRIDGE is decided first, so this is BRIDGE; an implementation that decided the target first would answer LATENCY and publish a target limit from a run that never measured the target |
| overrun alone, drops alone, both | three different findings with three different fixes, and the same total loss in two of them |
| exactly at the tolerance, one byte past it | the boundary a careless edit to the tolerance would move silently |
| three counters that exceed a 32-bit sum | refused, rather than wrapped into a plausible bridge loss |
| over-accounted | the target claims more than the host sent, which is a measurement defect and is refused rather than clamped |

`first_loss` is compared too, over four ramps, because that is where the
confusion would actually be published: a ramp whose bridge gives up at a high
rate must not read as the target failing there, and all four skip bridge rows and
take the lowest rate rather than the first row.

**The list was found to have a hole, by mutation, and the hole is worth
recording.** Reordering the Python's verdicts so the target was decided before
the bridge changed no answer in the list, because no step lost bytes in both
places at once. Three files claimed that order was load bearing and nothing
checked it. Two steps were added and the order is now asserted by name, and the
same mutation turns the suite red.
