"""P18's acceptance arithmetic in Python, the twin of c/dspaccept.c.

The same cascade in three formats, the same two metrics, the same refusals, and
no dependency beyond the standard library: the suite's whole requirements file is
one line, and an acceptance test that needs SciPy to decide a pass is not
available on the runner that decides merges.

It also carries, at the bottom, two sketches from chapter 18 exactly as printed.
Neither is used by anything above them. They exist so a test can demonstrate what
happens when they are run, rather than paraphrase it:

* ``chapter_snr_db`` is step 9's metric, which divides by the error energy and so
  divides by zero on any case where the two paths agree exactly.
* ``chapter_export_rows`` is step 3's coefficient export, which hands the design
  library's sections straight to the converter. It is the reason the Q15 path
  measures 15.02 dB against a tolerance of 55 dB: it leaves the whole passband
  gain in section one, where Q15 has no room for it.
"""
from __future__ import annotations

import math
import struct


def _to_f32(v):
    """Round one double to the nearest float32, with no numpy on the runner."""
    return struct.unpack("<f", struct.pack("<f", v))[0]


BIQUAD_MAX_STAGE = 8
BIQUAD_COEFF_PER_STAGE = 5
Q15_MAX_POST_SHIFT = 7

ERR_ARGUMENT = -1
ERR_STAGES = -2
ERR_SHIFT = -3
ERR_RANGE = -4
ERR_FINITE = -5
ERR_REFERENCE = -6

ERRORS = {"argument": ERR_ARGUMENT, "stages": ERR_STAGES, "shift": ERR_SHIFT,
          "range": ERR_RANGE, "finite": ERR_FINITE,
          "reference": ERR_REFERENCE}


def _stages_ok(coeffs, nstage):
    if nstage < 1 or nstage > BIQUAD_MAX_STAGE:
        return False
    return len(coeffs) >= nstage * BIQUAD_COEFF_PER_STAGE


def _finite(values):
    return all(math.isfinite(v) for v in values)


def _round_half_away_from_zero(v):
    return -math.floor(-v + 0.5) if v < 0.0 else math.floor(v + 0.5)


def _q15_scale(post_shift):
    scale = 32768.0
    for _ in range(post_shift):
        scale *= 0.5
    return scale


def _floor_shift(v, bits):
    """What an arithmetic right shift does, as the floor division it is."""
    return v // (1 << bits)   # Python's // already floors toward -infinity


def biquad_df2t(coeffs, nstage, data, single=False):
    """Transposed direct form 2, two state words per stage, state zeroed.

    ``single`` rounds every intermediate to float32 through struct, which is what
    makes this the twin of biquad_df2t_f32 rather than a second double path.
    """
    if coeffs is None or data is None or len(data) == 0:
        return ERR_ARGUMENT, None
    if not _stages_ok(coeffs, nstage):
        return ERR_STAGES, None
    used = coeffs[:nstage * BIQUAD_COEFF_PER_STAGE]
    if not _finite(used):
        return ERR_FINITE, None

    narrow = _to_f32 if single else (lambda v: v)
    out = [narrow(float(v)) for v in data]
    for s in range(nstage):
        b0, b1, b2, a1, a2 = (narrow(float(c)) for c in
                              used[s * BIQUAD_COEFF_PER_STAGE:
                                   (s + 1) * BIQUAD_COEFF_PER_STAGE])
        d1 = narrow(0.0)
        d2 = narrow(0.0)
        for i, x in enumerate(out):
            y = narrow(narrow(b0 * x) + d1)
            d1 = narrow(narrow(narrow(b1 * x) + narrow(a1 * y)) + d2)
            d2 = narrow(narrow(b2 * x) + narrow(a2 * y))
            out[i] = y
    return 0, out


def q15_post_shift(coeffs, nstage):
    """The smallest shift at which every coefficient fits, or a refusal."""
    if coeffs is None:
        return ERR_ARGUMENT, None
    if not _stages_ok(coeffs, nstage):
        return ERR_STAGES, None
    used = coeffs[:nstage * BIQUAD_COEFF_PER_STAGE]
    if not _finite(used):
        return ERR_FINITE, None
    for shift in range(Q15_MAX_POST_SHIFT + 1):
        scale = _q15_scale(shift)
        if all(-32768.0 <= _round_half_away_from_zero(c * scale) <= 32767.0
               for c in used):
            return 0, shift
    return ERR_RANGE, None


def q15_coeffs(coeffs, nstage, post_shift):
    """Convert at a stated shift, refusing anything that does not fit."""
    if coeffs is None:
        return ERR_ARGUMENT, None
    if not _stages_ok(coeffs, nstage):
        return ERR_STAGES, None
    if post_shift < 0 or post_shift > Q15_MAX_POST_SHIFT:
        return ERR_SHIFT, None
    used = coeffs[:nstage * BIQUAD_COEFF_PER_STAGE]
    if not _finite(used):
        return ERR_FINITE, None
    scale = _q15_scale(post_shift)
    rounded = [_round_half_away_from_zero(c * scale) for c in used]
    if any(v < -32768.0 or v > 32767.0 for v in rounded):
        return ERR_RANGE, None
    return 0, [int(v) for v in rounded]


def biquad_df1_q15(coeffs, nstage, post_shift, data):
    """Direct form 1 in Q15, returning (rc, samples, saturations)."""
    if coeffs is None or data is None or len(data) == 0:
        return ERR_ARGUMENT, None, 0
    if not _stages_ok(coeffs, nstage):
        return ERR_STAGES, None, 0
    if post_shift < 0 or post_shift > Q15_MAX_POST_SHIFT:
        return ERR_SHIFT, None, 0

    bits = 15 - post_shift
    saturations = 0
    out = [int(v) for v in data]
    for s in range(nstage):
        b0, b1, b2, a1, a2 = coeffs[s * BIQUAD_COEFF_PER_STAGE:
                                    (s + 1) * BIQUAD_COEFF_PER_STAGE]
        x1 = x2 = y1 = y2 = 0
        for i, x in enumerate(out):
            acc = b0 * x + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2
            y = _floor_shift(acc, bits)
            if y > 32767:
                y = 32767
                saturations += 1
            elif y < -32768:
                y = -32768
                saturations += 1
            x2, x1 = x1, x
            y2, y1 = y1, y
            out[i] = y
    return 0, out, saturations


def accept_metrics(got, ref):
    """Both metrics in one pass. Returns (rc, result) or (refusal, None)."""
    if got is None or ref is None or len(got) == 0 or len(got) != len(ref):
        return ERR_ARGUMENT, None
    if not _finite(got) or not _finite(ref):
        return ERR_FINITE, None

    n = len(ref)
    # Summed in order rather than with math.fsum, because the C adds in order
    # and the two implementations must agree to the last bit rather than to a
    # tolerance. A compensated sum here would be a better number and a worse
    # twin, and the twin is the point.
    mean_err2 = 0.0
    mean_ref2 = 0.0
    for g, r in zip(got, ref):
        e = g - r
        mean_err2 += e * e
        mean_ref2 += r * r
    mean_err2 /= float(n)
    mean_ref2 /= float(n)

    if mean_ref2 == 0.0:
        return ERR_REFERENCE, None
    if mean_err2 == 0.0:
        return 0, {"rel_rms": 0.0, "snr_db": math.inf, "exact": 1}
    return 0, {"rel_rms": math.sqrt(mean_err2) / math.sqrt(mean_ref2),
               "snr_db": 10.0 * math.log10(mean_ref2 / mean_err2),
               "exact": 0}


def section_gains(rows, nstage):
    """The b0 of each section, which is where a cascade's gain sits."""
    return [rows[s * BIQUAD_COEFF_PER_STAGE] for s in range(nstage)]


def redistribute_gain(rows, nstage, shares, tol=1e-12):
    """Rescale each section's numerator so its b0 becomes shares[s].

    The product of the shares must equal the product of the current b0 values,
    or the filter being returned is a different filter. That is checked rather
    than assumed, because this function exists to move quantisation error
    around and must not change the transfer function while doing it.

    This is the step chapter 18 does not have. Without it the whole passband
    gain of a design library's output sits in section one, and in Q15 there is
    no room for it there.
    """
    gains = section_gains(rows, nstage)
    if len(shares) != nstage:
        return ERR_ARGUMENT, None
    if any(g == 0.0 for g in gains) or any(s == 0.0 for s in shares):
        return ERR_ARGUMENT, None
    want = 1.0
    have = 1.0
    for s in shares:
        want *= s
    for g in gains:
        have *= g
    if abs(want - have) > tol * abs(have):
        return ERR_ARGUMENT, None

    out = list(rows[:nstage * BIQUAD_COEFF_PER_STAGE])
    for s in range(nstage):
        k = shares[s] / gains[s]
        for j in range(3):
            out[s * BIQUAD_COEFF_PER_STAGE + j] *= k
    return 0, out


def equal_shares(rows, nstage, ratio=1.0):
    """Shares that put the same gain in every section, tilted by ratio.

    ratio applies to the first section and its inverse to the last, so the
    product is unchanged and a sweep over ratio is a sweep over scalings of one
    filter. Two sections is the case chapter 18 designs.
    """
    have = 1.0
    for g in section_gains(rows, nstage):
        have *= g
    k = abs(have) ** (1.0 / nstage)
    if have < 0.0:
        k = -k
    shares = [k] * nstage
    shares[0] *= ratio
    shares[-1] /= ratio
    return shares


# ---------------------------------------------------------------------------
# Chapter 18 as printed, kept to be demonstrated against. Nothing above calls
# either of these.

def chapter_snr_db(a, b):
    """Step 9's metric exactly as printed.

    It divides by the error energy with no test for it, so two paths that agree
    exactly, which is every exact case in this project's oracle, divide by zero.
    In the chapter's NumPy it returns an infinity with a warning; in plain
    Python it raises. accept_metrics reports an exact flag instead.
    """
    err = [x - y for x, y in zip(a, b)]
    return 10.0 * math.log10(sum(y * y for y in b) / sum(e * e for e in err))


def chapter_export_rows(sos):
    """Step 3's export exactly as printed, for sections of six.

    The comment above it in the chapter is about the sign of the denominator,
    which this gets right. What it does not do is look at the magnitudes: a
    design library puts the whole passband gain in the first section, and this
    carries that straight into a format with 15 bits of fraction.
    """
    return [(s[0], s[1], s[2], -s[4], -s[5]) for s in sos]
