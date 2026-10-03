# P03 in C

**State: host only.** `rx_ring.c` is the receive path and `faults.c` reproduces
both documented library failure modes on purpose before repairing them. `rx.h` and
`usart3_ll.h` are the interface and the register-level binding.

Proven on the host against synthetic runs whose answers are known. Never built for
the target, so nothing is known about its behaviour on the board, and the project
README says which measurement is still missing.


## What it is proven against

The synthetic runs whose answers are known, which attribute every lost byte to
the target or to the bridge.