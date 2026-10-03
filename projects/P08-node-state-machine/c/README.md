# P08 in C

**State: host only, and proven there.** `node_sm.c` is eight states, nine events,
eighteen rows and a dispatcher that cannot block.

All eighteen transition rows are reachable in test, and the stub payload matches
P09's encoder byte for byte, which is what makes the stub a stub rather than a
placeholder.


## What it is proven against

All eighteen transition rows, each reachable, and a stub payload that matches
P09's encoder byte for byte.