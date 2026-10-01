# Rust on this part

**Nothing written yet.** This directory exists because Rust is a genuine option
on this exact device rather than an aspiration, and the research that established
that is worth not losing.

## What was verified for this part, not for the family name

The checks below were made against feature flags, board definitions and target
manifests naming this exact device, not the STM32H7 family. That distinction is
the whole point: most material that says "STM32H7" means the STM32H743, which is
a different part with a different reference manual.

| Piece | State for the STM32H7A3 |
|---|---|
| Peripheral access crate | has a module for this part |
| Hardware abstraction crate | has a feature flag for it, routed through its twin with the right manual's pin mapping |
| Async framework | names this exact package and pin count |
| Flashing and debugging tool | knows the exact die on this board, `STM32H7A3ZITxQ` |
| Real-time framework | works, because it schedules on the interrupt controller, which is architecture level rather than part level |

**The one gap, and it is an opportunity.** The async framework has no worked
example for this part, only for its twin. That is a contribution this volume
could make rather than a reason to avoid it.

## Why the toolchain question is different here

The C side of this repository is blocked on three downloads: `arm-none-eabi-gcc`,
a flashing tool, and `ninja`. Rust needs none of them. A target added to the Rust
toolchain plus the flashing tool is the whole requirement, and that flashing tool
is the same one the C side wants, so installing it unblocks both.

## What belongs here when it starts

The volume's Language variant axis assigns Rust to specific projects rather than
to all twenty, and the matrix in the appendix says which. P17 is the one that
owns the Rust variant as its subject: the same driver written twice, at the
vendor layer and at the register layer, with the Rust version as the third
reading.

A project here follows the same rule as everywhere else in this repository.
Nothing claims a measurement it has not taken, a value that is not confirmed
against RM0455 makes the code refuse rather than guess, and a figure that has not
been measured says "not measured".

## Why there is a folder per language at all

A reader opening this repository should see immediately what it is written in.
The four folders, `c/`, `cpp/`, `python/` and `rust/`, make the Language variant
axis of the volume navigable in the tree rather than only in an appendix table,
and they keep the Python that drives the C visible as what it is: harnesses,
generators and build scripts, not the deliverable.
