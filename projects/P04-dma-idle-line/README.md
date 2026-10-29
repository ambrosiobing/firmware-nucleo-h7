# P04: Circular DMA and the idle line

Status: c=host cpp=none python=host rust=host

**Started on the host, Monday 26 October 2026, and not on the board.** One part of
chapter 4 needs no hardware at all: the arithmetic that turns two transfer-counter
positions into the bytes that are new, as at most two spans neither of which
crosses the end of the buffer. That part is written, proven against a hand-written
oracle in two languages, and compiled under the whole warning set in CI.

**What it is.** The producer becomes a transfer engine and the head comes from the
transfer counter. The compiler-only barrier of P02 stops being sufficient here.

**What runs, and what it proves.**

| Piece | Where | What proves it |
|---|---|---|
| `c/rxspan.c`, the span split and the two guards | host and target alike | 18 oracle cases, a 4 096 pair exhaustive walk, and `-Wconversion -Wsign-conversion -Wpedantic`, which CI passes over it |
| `python/rxspan.py`, the twin | host | the same 18 cases, and a pairwise comparison with the C |
| `rust/src/lib.rs`, `no_std`, and `rust/src/main.rs`, the filter | host | the same 18 cases through a one verb protocol, and six unit tests |
| `vectors.json`, the oracle | the project owns it | checked against invariants that come from neither implementation |

**Why an oracle and not a copy of the implementation.** The eighteen cases were
written from chapter 4's steps 5 and 7 and then checked, before either
implementation was allowed to agree with them, against two things neither of them
knows: that the span lengths sum to the advance, and that no span reaches past the
end. The advance in that check is a modulo where both implementations use a mask,
**so a mistake in the masking cannot hide behind the oracle.**

**Three cases in it are there because they are the ones that get written wrong.**
A wrap that lands exactly on zero is one span and not two, because the second
would have zero length. An advance of exactly half the buffer is not a near lap
and one byte more is. And a buffer size that is not a power of two is refused
rather than masked, because masking by `size - 1` is the advance only when the
size is a power of two, and a 500 byte buffer would otherwise produce a plausible
wrong number.

**What is NOT done, which is most of the chapter.** The counter read itself, the
three interrupt handlers that all call one function, the cache invalidate the
canonical reference leaves out, the placement of the buffer in main memory rather
than in tightly coupled memory, and the measurement against chapter 3's path on
the same harness. **All of those need the board**, and the status line says `host`
rather than `board` for exactly that reason.

**The guard is not a detector and the chapter says so first.** There is no overrun
flag on this path. If the consumer falls behind a whole lap, the engine writes over
bytes nobody read and the arithmetic cannot tell that from no movement at all:
both arrive as the position being unchanged. What is offered is a bound and a
report, the peak one-step advance and a count of the steps that moved more than
half the buffer, and the half and full transfer interrupts are what keep the
function running often enough for the bound to mean something.

**What it needs on the bench.** Nucleo alone

**What has to come first.** P01 owns the cross toolchain file, the startup code
and the linker script, so nothing here can be flashed until that project exists,
which it does. The host half above needs none of it.

Chapter 04 of [the book][book] is the written design. When the board half starts
it follows that chapter's acceptance criteria, and anything the chapter leaves as
"not measured" is either measured or still says so.

[book]: ../../chapters/04-circular-dma-and-the-idle-line.md
