"""P14's record decoder in Python, the twin of c/recdecode.c.

Chapter 14 step 7 says to write this at the same time as the firmware, because
"a format with no second implementation is a format with no specification". The
same header parse, the same lookup through the record's own scale codes, the same
barometric relation, the same refusals, in the standard library alone.

**One comparison here is numeric rather than exact, and only one.** The altitude
raises a ratio to a fractional power, which is a library call in both languages,
and gcc's and CPython's need not agree in the last bit. So the parity test
compares altitudes to a relative 1e-12 and everything else exactly. The counts
and the header fields are integers; the milli-g and hectopascal conversions are a
single multiply or divide by a table entry, and the oracle's table entries are
powers of two so those are exact in binary.

It also carries, at the bottom, step 9's relation as printed and step 10's
arithmetic as printed. Neither is used above, and both are run by the tests.
"""
from __future__ import annotations

REC_HDR_BYTES = 14
REC_FS_CODES = 5
REC_BARO_NARROW = 0
REC_BARO_WIDE = 1
REC_BARO_CODES = 2

REC_FLAG_SHORT_WINDOW = 0x01
REC_FLAG_EXTENDED = 0x02
REC_FLAG_BARO_BLANKED = 0x04
REC_FLAG_KNOWN = 0x07

ERR_ARGUMENT = -1
ERR_FS_CODE = -2
ERR_BARO = -3
ERR_FLAGS = -4
ERR_COUNTS = -5
ERR_PRESSURE = -6

ERRORS = {"argument": ERR_ARGUMENT, "fs_code": ERR_FS_CODE,
          "baro": ERR_BARO, "flags": ERR_FLAGS, "counts": ERR_COUNTS,
          "pressure": ERR_PRESSURE}

ALT_SCALE = 44330.0
ALT_EXPONENT = 0.1903


def parse_header(buf):
    """Returns (0, header) or (refusal, None). Little endian, field order of
    step 7, parsed rather than cast."""
    if buf is None:
        return ERR_ARGUMENT, None
    if len(buf) < REC_HDR_BYTES:
        return ERR_ARGUMENT, None

    hdr = {
        "tick_event": int.from_bytes(bytes(buf[0:4]), "little"),
        "n_pre": int.from_bytes(bytes(buf[4:6]), "little"),
        "n_post": int.from_bytes(bytes(buf[6:8]), "little"),
        "n_baro": int.from_bytes(bytes(buf[8:10]), "little"),
        "lo_fs_g": buf[10], "hi_fs_g": buf[11],
        "baro_range": buf[12], "flags": buf[13],
    }
    if hdr["lo_fs_g"] >= REC_FS_CODES or hdr["hi_fs_g"] >= REC_FS_CODES:
        return ERR_FS_CODE, None
    if hdr["baro_range"] >= REC_BARO_CODES:
        return ERR_BARO, None
    if hdr["flags"] & ~REC_FLAG_KNOWN:
        return ERR_FLAGS, None
    if (hdr["flags"] & REC_FLAG_SHORT_WINDOW
            and hdr["flags"] & REC_FLAG_EXTENDED):
        return ERR_FLAGS, None
    if hdr["n_pre"] == 0 and hdr["n_post"] == 0:
        return ERR_COUNTS, None
    if hdr["n_baro"] == 0 and not hdr["flags"] & REC_FLAG_BARO_BLANKED:
        return ERR_COUNTS, None
    return 0, hdr


def scales(hdr, table):
    """The sensitivities the record asks for, from the caller's table."""
    if hdr is None or table is None:
        return ERR_ARGUMENT, None
    if hdr["lo_fs_g"] >= REC_FS_CODES or hdr["hi_fs_g"] >= REC_FS_CODES:
        return ERR_FS_CODE, None
    if hdr["baro_range"] >= REC_BARO_CODES:
        return ERR_BARO, None
    if not (table["lo_valid"] >> hdr["lo_fs_g"]) & 1:
        return ERR_FS_CODE, None
    if not (table["hi_valid"] >> hdr["hi_fs_g"]) & 1:
        return ERR_FS_CODE, None
    if not (table["baro_valid"] >> hdr["baro_range"]) & 1:
        return ERR_BARO, None

    out = {"lo_mg_per_lsb": table["lo_mg_per_lsb"][hdr["lo_fs_g"]],
           "hi_mg_per_lsb": table["hi_mg_per_lsb"][hdr["hi_fs_g"]],
           "baro_lsb_per_hpa": table["baro_lsb_per_hpa"][hdr["baro_range"]]}
    if not out["lo_mg_per_lsb"] > 0.0 or not out["hi_mg_per_lsb"] > 0.0:
        return ERR_FS_CODE, None
    if not out["baro_lsb_per_hpa"] > 0.0:
        return ERR_BARO, None
    return 0, out


def counts_to_mg(hdr, table, counts, fine):
    rc, s = scales(hdr, table)
    if rc != 0:
        return rc, None
    return 0, counts * (s["lo_mg_per_lsb"] if fine else s["hi_mg_per_lsb"])


def counts_to_hpa(hdr, table, counts):
    rc, s = scales(hdr, table)
    if rc != 0:
        return rc, None
    return 0, counts / s["baro_lsb_per_hpa"]


def altitude_m(p_hpa, p0_hpa):
    """Step 9's relation, with the reference as an argument."""
    if not p_hpa > 0.0 or not p0_hpa > 0.0:
        return ERR_PRESSURE, None
    return 0, ALT_SCALE * (1.0 - (p_hpa / p0_hpa) ** ALT_EXPONENT)


def altitude_resolution_m(lsb_per_hpa, p_hpa, p0_hpa):
    """Metres per least significant bit of pressure, analytically."""
    if not lsb_per_hpa > 0.0 or not p_hpa > 0.0 or not p0_hpa > 0.0:
        return ERR_PRESSURE, None
    slope = (ALT_SCALE * ALT_EXPONENT / p0_hpa
             * (p_hpa / p0_hpa) ** (ALT_EXPONENT - 1.0))
    return 0, slope / lsb_per_hpa


def drain_ms(samples, bytes_per_sample, overhead_bytes, bus_hz):
    """Step 10's arithmetic, with the overhead its comment names as an
    argument so the printed figure can be reproduced and corrected."""
    if samples == 0 or bytes_per_sample == 0:
        return ERR_COUNTS, None
    if not bus_hz > 0.0:
        return ERR_PRESSURE, None
    byts = float(samples) * float(bytes_per_sample) + float(overhead_bytes)
    return 0, 1000.0 * (byts * 9.0) / bus_hz


# ---------------------------------------------------------------------------
# Chapter 14 as printed. Nothing above calls either of these.

def chapter_altitude(p_hpa, p0_hpa):
    """Step 9 exactly as printed, including the name of its reference.

    There is nothing wrong with the arithmetic. What the chapter does not say is
    where p0 comes from, and the record defined in step 7 does not carry one:
    it carries both acceleration scales and the barometer range, for the stated
    reason that a record should be readable years later, and then omits the one
    other number the conversion in step 9 needs.
    """
    return 44330.0 * (1.0 - (p_hpa / p0_hpa) ** 0.1903)


def chapter_drain_ms():
    """Step 10's printed sum: 768 x 9 / 400000, in milliseconds.

    Its own comment says "plus address and register overhead" and the sum does
    not include any. The difference is small and the point is that the sentence
    and the arithmetic disagree.
    """
    return 1000.0 * (768.0 * 9.0) / 400000.0
