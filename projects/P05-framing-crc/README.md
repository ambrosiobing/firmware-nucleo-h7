# P05: framing and the hardware CRC unit

A frame format with a stated reason for every field, an encoder and decoder that
build unchanged on the host and on the target, and a Python twin that corrupts
frames on purpose and counts what gets through.

**State: the software half is written and proven. The peripheral half waits on
RM0455.** Twelve checks pass with no board attached, and this project removed a
`NotImplementedError` that P09's host listener had been shipping with.

## The format, and why each field is there

    [ COBS( payload || crc16(payload) ) ] [ 0x00 ]
                                             delimiter

**Why byte stuffing and not a length prefix.** A length prefix is cheap and
obvious, and it fails badly: corrupt the length and the receiver skips an
arbitrary number of bytes, so one bad byte costs several frames and there is no
reliable way back into step. Stuffing gives a byte value that cannot appear inside
a frame, so a receiver that has lost its place recovers at the next delimiter,
always, after losing exactly one frame. The cost is one overhead byte per 254
bytes of payload, plus one.

**Why the checksum is inside the stuffing.** If it were outside it could contain a
zero byte, and the delimiter would stop being unambiguous, which is the single
property the whole format is chosen for.

**The five-byte payload frames to nine bytes**, and the test derives that rather
than asserting it in prose: one stuffing byte, five of payload, two of checksum,
one delimiter. That nine is the number P09's argument for bit packing rests on.

## The checksum, and the four parameters that go wrong

| Parameter | Value |
|---|---|
| polynomial | `0x1021` |
| initial value | `0xFFFF` |
| input reflected | no |
| output reflected | no |
| final exclusive-or | none |

The first two are the ones everybody states. **The reversal settings are the ones
that go wrong**, because a checksum computed with the bits reflected is a perfectly
good checksum that disagrees with every published catalogue and with whatever is at
the other end of the link. The peripheral on this part can be configured either
way and its default is not necessarily the one above.

**The one line that makes this checkable from outside this repository.** The
published check value of these parameters over the nine bytes `123456789` is
`0x29B1`. Both implementations are held to it before anything else is believed,
and it is the first test in the file. Verified: both produce `0x29B1`.

## What is proven, and in rising order of what it proves

| Claim | How |
|---|---|
| The parameters are the ones claimed | both implementations reproduce the published `0x29B1` |
| The two implementations agree | identical frame bytes over 3000 random payloads, and each decoder accepts the other's encoder |
| Zeros survive the stuffing | payloads of all zeros, and no zero escapes into a frame |
| **Every single-bit corruption is rejected** | 3000 frames, one bit flipped in each, zero got through |
| Truncation is never partially delivered | 3000 truncated frames, none delivered as data |
| An injected delimiter is never read as data | a zero inside a frame is a refusal, not an interpretation |
| An idle line is not an error | nothing between two delimiters is discarded, not reported as corruption |

The single-bit claim is "every one" rather than "most", on purpose: that property
follows from the parameters, so one survivor would mean the parameters are not what
this project says they are.

The two implementations are also checked to **reject the same things**. A
disagreement there would be worse than either being wrong on its own, because the
stricter side would pass the suite while the more permissive one shipped.

## What it refuses

`crc16_hw` returns 0 and sets `*ok` to false while the peripheral's configuration
is unconfirmed against RM0455, and `crc16()` does **not** fall back to the software
reference when that happens. Falling back silently would hide a misconfigured
peripheral behind a correct answer, which would make the agreement test pass while
the hardware was wrong. A wrong checksum that looks right is the worst outcome here:
both ends agree with each other and with nothing else.

Also unconfirmed: whether the initial value must be written before or after the
peripheral's reset, which is a documented trap in this family.

## What this unblocked

P09's `python/P09/listen.py` shipped with two functions that raised
`NotImplementedError` and a note saying a frame decoder written from recollection
of another one is how two implementations come to disagree. P05 now provides the
real thing, the listener imports it, and it decodes a real P09 frame and rejects a
one-bit corruption of it:

    a real P09 frame: 082405FFFFE89C4100
    decode_frame:     ('ok', b'$\x05\xff\xff\xe8')
    one bit flipped:  ('checksum', None)

## Layout

    c/P05/crc16.{c,h}     the software reference, the arbiter, bitwise on purpose
    c/P05/cobs.{c,h}      byte stuffing, host and target alike
    c/P05/frame.{c,h}     the frame, and every way a decode can end
    python/P05/twin.py    the Python twin, and the bit flipper that attacks it
    python/tests/test_frame.py   twelve checks, no hardware

## Not done

- `crc16_hw.c` is not written: the peripheral's register offsets, its reversal
  configuration and the reset ordering are all RM0455's and none has been read.
- COBS is written here rather than vendored. The chapter names an MIT
  implementation and says to carry its copyright header, and vendoring means
  fetching that source rather than reproducing it from recollection. If it is
  later replaced by the real thing, that is a visible commit and the header comes
  with it.
- No cycle counts for either implementation, because nothing has run on the
  target. The comparison the chapter wants, peripheral against table against
  bitwise, needs a board.
- The decoder is a function over a complete frame. The two-state receiver that
  splits a live stream on the delimiter belongs with P03's ring and is not written.
