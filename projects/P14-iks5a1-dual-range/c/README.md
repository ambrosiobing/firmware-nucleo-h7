# P14 in C

**State: host, since Monday 26 October 2026.** Written on win11 aquamarine,
which compiles nothing and never will; the first compiler to see it is gcc in
WSL on the win11 skyhorizon demo laptop, bing@JPTOUPM678, through
`python3 python/tools/build_host.py`, and after that CI.

| File | What it is |
|---|---|
| `recdecode.h` | the record's contract, the three findings at length, the six refusals |
| `recdecode.c` | the byte-by-byte parse, the lookup through the record's codes, the relation, the drain |

**The sensitivity table is a parameter.** Chapter 14's step 9 converts counts
"using the scale codes in the header rather than a constant compiled into the
script". That argument applies to this file too: a table of sensitivities
compiled in here would be the same constant one level down, and neither sensor's
datasheet is on this bench. So the caller supplies the table, each entry carries
a valid bit, and a code the table does not define is refused rather than read out
of whatever is in the slot.

**The header is parsed rather than cast.** A packed structure is the right way
for the firmware to write the record and the wrong way for a host to read it: a
cast assumes the host's byte order and its tolerance for unaligned members, and a
second implementation that shares an assumption with the first is not a second
implementation. The field order and the little-endian byte order are the
contract, and they are spelled out.

**Two refusals are not in the chapter.** A window flagged both short and
extended cannot be both, and step 7 defines the two bits separately without
saying they are exclusive. A record with no acceleration samples on either
channel is a header without a record. Both are the kind of thing writing the
decoder in the same sitting is supposed to surface.

## What it is proven against

11 header cases, 6 conversions, 5 altitudes, 3 resolutions, 4 drain times and 8
refusals, driven through this implementation and its Python twin by
`python/tests/test_recdecode_parity.py`. Integers are compared exactly; the
altitude raises a ratio to a fractional power, which is a library call, so those
are compared to a relative 1e-12. The C half of that test skips on a laptop with
no compiler, naming the WSL command, and runs in full in CI.

## What is not here

Both channels proved live, the watermark line, the pre-trigger ring, the arming
on the high-g line, the price of changing the barometer's full scale, the
foam-pad event, the sealed bag and the rate at which the bus stops keeping up.
All of them need the X-NUCLEO-IKS5A1 shield, which the bench holds; see
[../README.md](../README.md) for what the board half is actually waiting on,
which is the I2C pin mapping rather than the part.

[../README.md](../README.md) is the project, its status line and the chapter it
implements.
