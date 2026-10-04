# P01 in C++

**Nothing here yet.** The same image in C++ is the first thing this directory
carries: the same LEDs, the same banner, the same clock decode, compiled as C++17
with no exceptions, no RTTI and no heap, linking the same C board support.

What it is for. It is the cheapest honest answer to what C++ costs on this part,
because the two images differ in language and in nothing else, so the difference
in flash bytes and in the banner's cycle count is the cost of the language rather
than of the program.


## What it is proven against

**Two things, since Sunday 4 October 2026, and the host one comes first.**

The host oracle is [`../clock_vectors.json`](../clock_vectors.json): 15 register
configurations in, four frequencies or a named refusal out, plus 8 rows of the
signed bias between a reported and a true frequency. That file did not exist
until the clock tree decode moved out of `c/board/system.c` and became callable
without a part, and it is what makes a C++ half of this project possible at all
before anything is flashed. The C++ implementation goes in as a filter driven by a parity test that does
not exist yet, in the same shape P05, P06, P08 and P12 already use and which
`python/tests/test_gates_parity.py` is the clearest example of.

The board itself is the second: LD1 green on PB0 blinking, the banner on COM13 at
115200, and the clock the part reports about itself.