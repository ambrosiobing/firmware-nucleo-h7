# codec/fields.py: the only place the layout is written.
#
# This file is the specification. Nothing here encodes anything: it is widths,
# signedness and order, and the C header and the Python module are both
# generated from it by tools/gen_codec.py.
#
# Order is most significant bit first. Bit 39 is the first bit on the wire.
# docs/bitorder.md is the normative prose statement of what that means, and it
# was written before either implementation existed.

FIELDS = [
    # name        bits  signed  note
    ("version",    3,   False,  "format version, 0 to 7"),
    ("flags",      4,   False,  "bit 0 retry, bit 1 forced, bits 2 and 3 spare"),
    ("sequence",   9,   False,  "wraps at 512"),
    ("feature",   18,   True,   "two's complement, scaled by 1/16"),
    ("battery",    6,   False,  "units of 50 mV above 2.00 V"),
]

TOTAL_BITS = sum(f[1] for f in FIELDS)     # 40
assert TOTAL_BITS % 8 == 0, "the payload must be a whole number of bytes"
assert TOTAL_BITS == 40, "layout changed: recompute test/vectors.json by hand"

# The second assertion is deliberately a tripwire rather than a convenience.
# Changing a width changes every golden vector in test/vectors.json, and those
# vectors are computed by hand on purpose. A build that regenerated them from
# the new widths would be asserting that the generator agrees with itself,
# which is the one thing the vectors exist to not prove.
