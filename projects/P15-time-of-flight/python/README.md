# P15 in Python

**State: host, since Monday 26 October 2026.** The twin of
[`../c/zonetrack.c`](../c/zonetrack.c): the same algorithm, the same refusals and the
same numbers, in the standard library alone. The suite's whole requirements file
is one line, and a check that needed a numerical package to decide a pass would
not run on the runner that decides merges.

| File | What it is |
|---|---|
| `zonetrack.py` | the hysteresis, which is the part that stops the status byte changing state every time a person shifts their weight |

**What is deliberately not modelled here** is the validity filter, the reduction from 64 zones to one distance, and the status byte. Each needs constants the chapter defines nowhere, and inventing them would be fiction dressed as a specification.

## What it is proven against

19 oracle cases, plus a 600 frame property that a target oscillating inside the gap moves the state zero times, with its own falsifier showing that the same target with a 1 mm gap chatters, driven through this implementation and the C by
`python/tests/test_zonetrack_parity.py`. This half runs everywhere, including on a
laptop with no compiler, which is why the oracle is checked here first.

## What is not here

The sensor, the upload of its firmware, the watchdog window and the hour-long run. All of them need the IKS4A1 shield.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
