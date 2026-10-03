# P12 in Python

**State: host only, and host by nature rather than by omission.**

`gates.py` is the two gates, in increasing order of what they cost to run, so
the cheap one fails first. The fixture files it reads are in this project
directory, and `python/tests/test_gates.py` proves that a five percent charge
regression turns the build red with nobody at the bench.

This is the one project where Python is the deliverable rather than a twin: the
gate runs on a continuous integration runner, which has no board.


## What it is proven against

The fixture files in this directory, and the criterion that a five percent
charge regression turns the build red.