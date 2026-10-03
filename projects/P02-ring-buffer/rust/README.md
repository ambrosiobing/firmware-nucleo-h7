# P02 in Rust

**Nothing here yet.** The ring in Rust with `core::sync::atomic` and explicit
`Ordering`, which gives the fifth mode the C side has to add by hand: acquire and
release expressed in the type system rather than in a macro.

The measurement is the same measurement: the same four modes, the same soak, the
same cycle and byte costs on the same board, so the three languages are compared
and not merely listed.


## What it is proven against

The four-mode soak: 21.4 million bytes through the ring with zero mismatches,
and a cycle and byte cost per ordering mode.