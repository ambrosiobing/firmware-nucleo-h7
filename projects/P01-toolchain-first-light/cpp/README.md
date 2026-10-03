# P01 in C++

**Nothing here yet.** The same image in C++ is the first thing this directory
carries: the same LEDs, the same banner, the same clock decode, compiled as C++17
with no exceptions, no RTTI and no heap, linking the same C board support.

What it is for. It is the cheapest honest answer to what C++ costs on this part,
because the two images differ in language and in nothing else, so the difference
in flash bytes and in the banner's cycle count is the cost of the language rather
than of the program.


## What it is proven against

The board itself: LD1 green on PB0 blinking, the banner on COM13 at 115200, and
the clock the part reports about itself.