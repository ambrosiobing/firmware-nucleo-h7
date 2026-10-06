# P01's filter protocol: the clock tree decode, one request per line

The C++ and Rust implementations of this project's clock tree decode are driven
as filters, one request per line on standard input and exactly one answer per
line on standard output. `python/tests/test_clocktree_parity.py` drives both with
the same code, which is only possible because they speak the same protocol, so
this file is the contract rather than a description of one.

The C is driven through `ctypes` instead, against the shared library
`build-host/libclocktree.so`, because it is the implementation the board links
and there is no reason to put a text layer between the test and the object file.
The Python is imported directly. So four languages, three routes, one oracle:
[`clock_vectors.json`](clock_vectors.json).

## Why a filter and not four libraries

A C++ or Rust shared library would need a stable C ABI for a struct of seven
registers and a struct of five results, maintained in three places. The filter
needs a line of decimal integers. P05, P06, P08 and P12 all made this choice and
the reasoning is the same here: the thing under test is the decode, and an ABI
would become a second thing under test.

## Two verbs

Every field is a **decimal** integer. Not hexadecimal, although the registers are
naturally written in hex and the oracle's `why` fields discuss them that way: one
base throughout means no implementation needs a second parser, and a `0x` prefix
that one language accepts and another does not is exactly the kind of difference
that would show up as a disagreement about the clock tree.

### `D` decode

    D <cr> <cfgr> <pllckselr> <pllcfgr> <pll1divr> <cdcfgr1> <cdcfgr2> <hsi_nominal> <hse_bypass>

The seven register values in the order `clocktree_regs_t` declares them, then the
two input frequencies in hertz. Nine fields after the verb.

The answer is six fields:

    <sys_hz> <core_hz> <ahb_hz> <pclk1_hz> <pclk2_hz> <refusal>

`pclk2_hz` arrived on Tuesday 6 October 2026, which changed this line from five
fields to six. TIM1 sits on APB2 and the frequency counter's self test is driven
by TIM1, so the decode had to report that bus. It cost no new input: CDPPRE1 is
bits 6:4 of `cdcfgr2` and CDPPRE2 is bits 10:8, so the nine request fields are
unchanged and only the answer grew.

The change was made in all four implementations and in the oracle in one commit,
because a protocol with four speakers has no version negotiation and does not
need one while they are versioned together in one repository.

`refusal` is one of nine tokens, exactly as `clocktree_refusal_text` returns
them, and when it is anything but `ok` the four frequencies are all `0`:

    ok  sws-unknown  hse-not-bypass  pll-fractional  pll-p-disabled
    pll-source-unknown  pll-zero-divider  pll-would-overflow
    prescaler-undecoded

The tokens are compared, not just the numbers. That is the point of having them:
two implementations can agree that a configuration is unusable and disagree about
which guard caught it, and a comparison of frequencies alone would call that
agreement.

### `B` bias

    B <reported_hz> <true_hz>

The answer is four fields:

    <ok> <frequency_ppm> <duration_ppm> <delay_ppm>

`ok` is `1` or `0`. It is `0` when either frequency is zero, and then the three
parts-per-million figures are all `0`, because a bias against an unknown
frequency is not a small bias.

The three figures are signed. They are **two** quantities and not three:
`delay_ppm` always equals `frequency_ppm`, by the same expression, because both
divide by the true frequency; `duration_ppm` divides by the reported one instead.
See `c/clock/clocktree.h`.

## The arithmetic each implementation has to reproduce

These are the four places where four languages can differ while each looking
correct, and each has a row in the oracle.

| | |
|---|---|
| **divide before multiply** | the PLL reference is divided by `M` and then multiplied by `N`, so a remainder is lost before the multiply. The opposite order gives 373333333 where this gives 373333240 |
| **truncate toward zero** | every frequency is an unsigned integer division. Python's `//` floors, which is the same thing for these, but the parts-per-million arithmetic below is signed and there it is not |
| **halves away from zero** | the parts-per-million figures round halves away from zero, which neither C nor Rust nor Python gives for free. One hertz above 2 MHz is exactly half a part per million and the answer is 1, not 0 |
| **saturate, do not refuse** | a ratio far enough from one to overflow a signed 32-bit count of parts per million returns the extreme rather than a second kind of refusal, so there is one refusal path and not two |

## Failure, and why it is loud

A malformed request is refused on standard output and the process exits
non-zero. It never guesses and it never silently skips a line, because a skipped
line misaligns every answer after it, and the parity test would then report a
disagreement about the clock tree when the real fault was a parser.

Both filters read all of standard input and then split on newlines, rather than
reading a line at a time into a fixed buffer. That is a lesson and not a
preference: P08's C++ filter used a 4096-byte buffer, met a request of twenty one
thousand characters on Saturday 3 October 2026, and answered twenty requests with
twenty five answers, having split one long line into several that each parsed
into something.
