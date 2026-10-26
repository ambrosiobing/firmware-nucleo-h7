"""P10's per-phase charge in Python, the twin of c/phaseledger.c.

The same run-length decode by sample index, the same two totals by different
routes, the same residual and the same refusals.

It also carries, at the bottom, the chapter's sketch exactly as printed, which is
not used by the ledger and exists so a test can demonstrate what is wrong with it.
"""
from __future__ import annotations

PHASE_CODES = 8

ERR_ARGUMENT = -1
ERR_INTERVAL = -2
ERR_FLOOR = -3
ERR_CODE = -4

ERRORS = {"argument": ERR_ARGUMENT, "interval": ERR_INTERVAL,
          "floor": ERR_FLOOR, "code": ERR_CODE}


def ledger(current_a, codes, dt_s: float, min_samples: int):
    """Return (0, result) or (refusal, None)."""
    if current_a is None or codes is None or len(codes) == 0:
        return ERR_ARGUMENT, None
    if len(current_a) != len(codes):
        return ERR_ARGUMENT, None
    if not dt_s > 0.0:
        return ERR_INTERVAL, None
    if min_samples == 0:
        return ERR_FLOOR, None
    if any(c >= PHASE_CODES or c < 0 for c in codes):
        return ERR_CODE, None

    n = len(codes)
    per = [0.0] * PHASE_CODES
    runs = rejected = rejected_samples = 0

    total = sum(current_a) * dt_s          # no reference to the markers

    split = 0.0
    start = 0
    for i in range(1, n + 1):
        if i == n or codes[i] != codes[start]:
            count = i - start
            if count >= min_samples:
                q = sum(current_a[start:i]) * dt_s
                per[codes[start]] += q
                split += q
                runs += 1
            else:
                rejected += 1
                rejected_samples += count
            start = i

    residual = abs(total - split) / abs(total) if total != 0.0 else 1.0
    return 0, {"per_code": per, "total": total, "split": split,
               "residual": residual, "runs": runs, "rejected": rejected,
               "rejected_samples": rejected_samples}


def chapter_sketch_intervals(codes, dt: float):
    """Chapter 10 step 7 exactly as printed, kept to be demonstrated against.

    This is NOT used by ledger above. Its filter compares a difference of two
    products of dt against dt, so whether a single-sample run survives is decided
    by floating-point rounding rather than by its length. A test shows that it
    keeps some single-sample runs and rejects others in the same trace.
    """
    out, start, cur = [], 0.0, codes[0]
    for i in range(1, len(codes)):
        if codes[i] != cur:
            out.append((cur, start, i * dt))
            start, cur = i * dt, codes[i]
    out.append((cur, start, len(codes) * dt))
    return [iv for iv in out if (iv[2] - iv[1]) >= dt]
