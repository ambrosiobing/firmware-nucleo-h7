# P13: The IKS4A1, FIFO, watermark, interrupt

Status: c=host cpp=host python=host rust=host

**Started on the host, Monday 26 October 2026.** Three pieces of chapter 13 are
arithmetic on a block of bytes and need no shield: the bounded decode of a
drained buffer into per-source counts, the drop count recovered from the tag
counter's sequence, and the fill-to-drain ratio that decides whether a watermark
is safe. All three are written, and all three turned out to be wrong in the
chapter in ways a ten-minute run would not have revealed.

**What it is.** A logger that never polls a sensor, driven entirely by the
sensor's own FIFO watermark interrupt.

## What runs

| Piece | What proves it |
|---|---|
| `c/fifodecode.c`, the decode, the gap and the ratio | 9 decode cases, 25 gap rows, 6 refusals, 4 budgets, 6 budget refusals |
| `python/fifodecode.py`, the twin, standard library only | the same cases, and both of the chapter's sketches kept unused |
| `rust/src/lib.rs`, `no_std` with `f64`, and `rust/src/main.rs`, the filter | the same cases through a three verb protocol, six unit tests, and finding two carried by `Option<u8>` rather than by a flag beside a counter |
| `cpp/fifodecode.hpp` and `cpp/fifodecode_filter.cpp` | the same cases through the same three verb protocol, with `std::optional` carrying finding two and a sized vector replacing the pointer and count |
| `vectors.json`, the oracle | **integer expectations written by hand from the field layout**, each one checked against the implementation before it was emitted |

**The decode expectations are exact, because they are counts.** Nothing in them
passes on a tolerance. The generator refused to emit any case whose hand-written
numbers disagreed with the implementation, and one case is named after that check
working: a counter that appears to go backwards is a gap too, and a larger one
than it looks, so 0, 2, 1, 3 in a four-value field is four drops across three
gaps rather than two of each.

**The field positions are a stated layout and not a claim about the part.**
Chapter 13 writes `TAG_SHIFT`, `CNT_SHIFT` and `CNT_MASK` as symbols, and its
Sources section sends the tag format to each sensor's own datasheet with the note
that the depth "is not assumed from a sibling part". Only the word shape is in
the chapter, a tag byte and six data bytes. So the layout is a parameter here and
**every finding below holds for any counter field narrower than a byte** rather
than for one guessed width.

## Step 7's drop counter cannot report zero drops on a clean buffer

Step 9's acceptance criterion is zero drops over a ten-minute run, counted on the
board. Step 7 recovers them from the tag counter:

```c
if (cnt != expect_cnt) drops += (uint8_t)(cnt - expect_cnt);
expect_cnt = (uint8_t)(cnt + 1u);
```

Both lines work in a byte's modulus whatever the field's width is. The second is
the worse of the two: once the field has wrapped, `expect_cnt` holds a value the
field can never produce, so every later comparison is wrong. On a **perfectly
clean** buffer with a two-bit counter:

| Clean slots | Wraps crossed | This decode | Step 7 as printed |
|---|---|---|---|
| 4 | 0 | 0 drops | 0 drops |
| 8 | 1 | 0 drops | **252 drops** |
| 12 | 2 | 0 drops | **504 drops** |
| 16 | 3 | 0 drops | **756 drops** |
| 40 | 9 | 0 drops | **2268 drops** |

At the chapter's own 208 entries a second, a ten-minute run would report about
7.9 million drops from a flawless buffer. **At a counter width of eight bits the
printed line is correct**, which is the shape of the finding: the two moduli
coincide there and every narrower field makes it worse. Of the 25 gap rows in the
oracle, 11 disagree with step 7, the worst being a one-bit counter where one lost
slot is reported as 255.

**It also fabricates drops on the first drain.** `expect_cnt` is a static
beginning at zero with no flag for the first word, and the buffer has been
batching since it was configured, so a first drain opening at counter three is
measured against a value nobody wrote. This decode lets the first word establish
the sequence, which is two lines.

**And a gap of exactly the counter's modulus is invisible**, in any
implementation, because the counter has returned to where it was expected. That
is a bound rather than a defect, it is not in the chapter, and `fifo_budget`
reports it beside the number so the second drop mechanism's blind spot travels
with its result.

## Step 8's fill time divides by one source's rate

| | Fill time | Ratio against the drain |
|---|---|---|
| step 8 as printed, 128 / 104 Hz | 1230.77 ms | 60.9 |
| two sources batched, 128 / 208 entries per second | **615.38 ms** | **30.4** |

Step 4 batches the accelerometer and the gyroscope at 104 Hz each, step 6 decodes
both tags, and step 8 prints `rate 104 Hz watermark 128 words fill 1230 ms`,
which is 128 / 104 to the millisecond. Every batched source writes its own
entries into one buffer, so it fills at the sum of the rates and the safety
margin step 8 requires is **optimistic by the number of batched sources**. With a
single source the two forms are identical, which is the control: the difference is
the source count and not this implementation.

Both of the chapter's sketches are carried unused in `python/fifodecode.py` so
the tests run them rather than paraphrase them.

## What is not here

The bus shim and its two functions, the identification registers of step 3, the
batching and watermark configuration of step 4, the empty handler of step 5, the
sensor's own overrun flag, the ten-minute run of step 9 and the comparison
against polling in step 10. **There is no measured number anywhere in this
project**, and the status line says host for that reason. The decode cost per word
that `fifo_budget` takes as a parameter is exactly what step 8 says to measure
with the cycle counter rather than assume, and it has not been measured.

**What it needs on the bench.** The NUCLEO-H7A3ZI-Q and the X-NUCLEO-IKS4A1
shield, **which the bench does hold**, on the Arduino header as the only shield
on the board. This paragraph said the inventory does not hold it until Thursday
29 October 2026, and that was simply wrong: the shield has been on the bench
throughout, along with the X-NUCLEO-IKS5A1, the X-NUCLEO-53L8A1 and the
STEVAL-STWINBX1. The board half is not blocked on a part. What it is blocked on
is the bus: chapter 13's peripheral table writes the I2C pins as Arduino D14 and
D15 **by convention, to be checked against the MB1363 user manual**, and nothing
in this repository has read those pins yet.

Chapter 13 of [the book][book] is the written design.

[book]: ../../chapters/13-the-iks4a1.md
