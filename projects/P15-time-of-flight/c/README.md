# P15 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI, which compiles the same
source with that whole warning set. It is not in the workflow's
warnings-as-errors step yet: it compiles clean under `-Werror` on gcc 15.2.0,
and adding it there turned the runner's own gcc red on Monday
26 October 2026, so that step waits on the diagnostic.

| File | What it is |
|---|---|
| `zonetrack.h` | four states, six edges, and the thresholds as parameters rather than constants |
| `zonetrack.c` | the hysteresis, which is the part that stops the status byte changing state every time a person shifts their weight |

**The specification came from figure 15.4 rather than from step 9's sketch**, because the figure carries a third threshold at 1500 mm that the sketch omits. The thresholds are parameters because the figure's own note calls them a starting point to be measured.

## What it is proven against

19 oracle cases, plus a 600 frame property that a target oscillating inside the gap moves the state zero times, with its own falsifier showing that the same target with a 1 mm gap chatters, driven through this implementation and its Python twin by
`python/tests/test_zonetrack_parity.py`. The C half of that test skips on a laptop
with no compiler, naming the WSL command, and runs in full in CI.

## What is not here

The sensor, the upload of its firmware, the watchdog window and the hour-long run. All of them need the IKS4A1 shield.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
