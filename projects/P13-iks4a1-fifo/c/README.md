# P13 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI.

| File | What it is |
|---|---|
| `fifodecode.h` | the layout as a parameter, the four findings at length, the five refusals |
| `fifodecode.c` | the bounded decode, the gap in the counter's own modulus, the fill-to-drain ratio |

**Why the field widths are arguments rather than constants.** Chapter 13 writes
`TAG_SHIFT`, `CNT_SHIFT` and `CNT_MASK` as symbols and sends the tag format to
each sensor's datasheet, which this bench does not have. Taking the layout as a
struct is what lets the findings be stated for any counter narrower than a byte
instead of for one guessed width, and it is why the gap function is exposed and
tested on its own across five widths.

**The word count is refused and not clamped.** Step 6 clamps the buffer level to
the landing buffer, which is right, and this function refuses a count above its
bound instead of quietly shortening it: a level register that can drive a read
past the end of a buffer should produce an error at the call rather than a
shorter answer that looks ordinary.

## What it is proven against

9 decode cases, 25 gap rows across five counter widths, 6 format refusals, 4
watermark budgets and 6 budget refusals, driven through this implementation and
its Python twin by `python/tests/test_fifodecode_parity.py`. The C half of that
test skips on a laptop with no compiler, naming the WSL command, and runs in full
in CI.

## What is not here

The bus shim, the identification registers, the batching and watermark
configuration, the interrupt route, the empty handler, the overrun flag, the
ten-minute run and the comparison against polling. All of them need the
X-NUCLEO-IKS4A1 shield, which the bench holds; see
[../README.md](../README.md) for what the board half is actually waiting on,
which is the I2C pin mapping rather than the part.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
