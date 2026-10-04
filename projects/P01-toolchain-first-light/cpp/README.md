# P01 in C++

**The host half is here and the board half is not**, which is a sharper
statement than "partly done" and is what the status line says: `cpp=host`.

`clocktree.hpp` is this project's clock tree decode in C++17, header-only, with
no exceptions, no RTTI and no heap, so the same file could be linked into a
firmware image. It is not linked into one today. `clocktree_filter.cpp` drives it
over the protocol in [`../PROTOCOL.md`](../PROTOCOL.md), which the Rust filter
also speaks, so one parity test drives both.

**Where it is deliberately not a transliteration of the C.** The refusal is an
`enum class`, so it cannot be compared with a frequency by accident; the result
comes back by value rather than through an out parameter, because the C returns
through a pointer for a reason that applies to `SystemInit` and not to a host;
and the token is a `std::string_view` over the same literals. The arithmetic is
identical, because the arithmetic is the specification.

**And the field positions are written out again rather than included.** That is
the point of a second implementation. One that read `c/board/stm32h7a3_regs.h`
could not disagree with the C about where a field sits, so it could not catch the
error it exists to catch: that `DIVM1` holds the value while `N1` and `P1` hold
the value minus one.

Still to come, and unchanged in intent: the same image in C++, the same LEDs and
banner and clock decode, linking the same C board support. It is the cheapest
honest answer to what C++ costs on this part, because the two images would differ
in language and in nothing else.


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

The board itself is the second and is not reached yet: LD1 green on PB0
blinking, the banner on COM13 at 115200, and the clock the part reports about
itself.