# P03 in Python

**State: host only, and this is the harness half of the project.**

`loadgen.py` sends at a rate the test chooses. `report.py` attributes every lost
byte to the target or to the bridge, which is the part of the measurement worth
protecting, and `python/tests/test_report.py` proves the attribution on runs whose
answers are known before any byte crosses a wire.

Neither file touches a device. `loadgen.py` needs `pyserial`, which is in
`requirements-hardware.txt` and which the suite never installs.


## What it is proven against

The synthetic runs whose answers are known, which attribute every lost byte to
the target or to the bridge.