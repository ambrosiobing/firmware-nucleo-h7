# P06 in Python

**State: host only, and it is the witness rather than the subject.**

`scan.py` is the analysis that decides whether a rate claim stands, and it is
proven on synthetic input before it is ever pointed at a capture. The two
independent routes it must agree with are in `python/firmkit/rate.py`.

A clean square wave at the wrong rate is indistinguishable from a right one
without an external witness, which is this project's whole subject.


## What it is proven against

The external witness: an edge analysis and a rate analysis that must agree
before any rate is claimed.