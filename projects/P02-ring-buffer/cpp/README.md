# P02 in C++

**Nothing here yet.** The C++ version of the four ordering modes, using
`std::atomic` with `memory_order_relaxed`, `memory_order_acquire` and
`memory_order_release` where the C version uses its own barrier macros.

Why it is worth the space. The C side reaches its four modes through a build-time
macro; the C++ side names the ordering in the type. If the two measure the same
cycle and byte costs, the barrier macros are proven to say what the standard
library says. If they differ, one of the two is wrong and the difference is the
chapter.


## What it is proven against

The four-mode soak: 21.4 million bytes through the ring with zero mismatches,
and a cycle and byte cost per ordering mode.