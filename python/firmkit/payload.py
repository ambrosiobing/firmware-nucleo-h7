# codec/payload.py: the Python twin.
#
# This one file serves three places, which is the point of the chapter:
#   - the property test on the host, as the oracle for the C encoder
#   - host/listen.py, as the decoder at the far end of the framed link
#   - the board itself under MicroPython, unmodified
#
# The third of those constrains the style. No f-strings in the hot path, no
# type annotations that need a module the board firmware does not carry, no
# imports beyond the generated field table. Everything here is in the
# interpreter's subset on purpose.

try:                                   # the board: two flat files, no packages
    from payload_fields import FIELDS, TOTAL_BITS, TOTAL_BYTES
except ImportError:                     # the host: firmkit is a package
    from .payload_fields import FIELDS, TOTAL_BITS, TOTAL_BYTES


def _bits(data, start, width):
    """Read width bits from data, most significant bit first."""
    v = 0
    for i in range(width):
        b = start + i
        v = (v << 1) | ((data[b >> 3] >> (7 - (b & 7))) & 1)
    return v


def encode(fields):
    """Pack a dict of field values into TOTAL_BYTES bytes.

    Values are masked to their width rather than rejected, matching
    payload_encode in C. A sequence counter that wraps at 512 is meant to
    wrap.
    """
    out = bytearray(TOTAL_BYTES)
    pos = 0
    for name, width, signed, _ in FIELDS:
        if name not in fields:
            raise ValueError("missing field: " + name)
        v = fields[name] & ((1 << width) - 1)
        for i in range(width):
            if (v >> (width - 1 - i)) & 1:
                b = pos + i
                out[b >> 3] |= 0x80 >> (b & 7)
        pos += width
    return bytes(out)


def decode(data):
    """Unpack TOTAL_BYTES bytes into a dict of field values."""
    if len(data) * 8 != TOTAL_BITS:
        raise ValueError("expected " + str(TOTAL_BYTES) + " bytes, got " + str(len(data)))
    out = {}
    pos = 0
    for name, width, signed, _ in FIELDS:
        v = _bits(data, pos, width)
        if signed and (v >> (width - 1)) & 1:
            v -= 1 << width                      # two's complement, explicit
        out[name] = v
        pos += width
    return out


def limits(name):
    """The inclusive range of a field, used by the test to generate cases."""
    for fname, width, signed, _ in FIELDS:
        if fname == name:
            if signed:
                return (-(1 << (width - 1)), (1 << (width - 1)) - 1)
            return (0, (1 << width) - 1)
    raise KeyError(name)
