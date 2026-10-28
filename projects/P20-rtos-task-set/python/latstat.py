"""P20's latency histogram, its percentiles and the budget verdict, the twin of
c/latstat.c.

Chapter 20's acceptance is the three latency rows of table 20.4, written before
the first run. The figures come from the board; the arithmetic that turns them
into a verdict does not, and it is what decides whether the chapter passes.
Standard library only.

**This twin has no fixed width and no implicit truncation, and that is the
point.** The C computes the 99.9th percentile's rank as a 64-bit product
because 999 times a count near four thousand million overflows 32 bits. Python
would never notice, so the oracle carries a count the 32-bit product cannot
take and the parity test is what holds the C to the twin's arithmetic.

It also carries, at the bottom, the two verdicts the chapter's own wording
invites and this project refuses: `chapter_two_way_verdict`, which compares a
percentile with its budget as though the percentile were a value rather than a
bin, and `chapter_used_from_kernel`, which reads the kernel's free-stack figure
as though it were the used figure. Nothing above calls either, and a test runs
both to show what they would have reported.
"""
from __future__ import annotations

LAT_BINS = 1024

LAT_BUDGET_MEDIAN = 100
LAT_BUDGET_P999 = 250
LAT_BUDGET_MAX = 1000

UINT32_MAX = 0xFFFFFFFF

LAT_PASS = 0
LAT_FAIL = 1
LAT_UNDECIDED = 2

VERDICTS = {"pass": LAT_PASS, "fail": LAT_FAIL, "undecided": LAT_UNDECIDED}

ERR_ARGUMENT = -1
ERR_RATE = -2
ERR_FULL = -3
ERR_EMPTY = -4
ERR_COUNT = -5
ERR_STACK = -6

ERRORS = {"argument": ERR_ARGUMENT, "rate": ERR_RATE, "full": ERR_FULL,
          "empty": ERR_EMPTY, "count": ERR_COUNT, "stack": ERR_STACK}


def new_hist():
    """An empty histogram, the same four fields as lat_hist_t."""
    return {"bins": [0] * LAT_BINS, "over": 0, "max_us": 0, "count": 0}


def record(hist, cycles, cycles_per_us):
    """One sample. Returns 0 or a refusal.

    The division truncates toward zero, which is step 5's own expression, and
    the bias is downward, which is why the verdict below will not pass a bin
    that straddles its budget.
    """
    if hist is None:
        return ERR_ARGUMENT
    if cycles_per_us == 0:
        return ERR_RATE
    if hist["count"] == UINT32_MAX:
        return ERR_FULL

    us = cycles // cycles_per_us
    if us < LAT_BINS:
        hist["bins"][us] += 1
    else:
        hist["over"] += 1
    if us > hist["max_us"]:
        hist["max_us"] = us
    hist["count"] += 1
    return 0


def _rank_of(count, numer, denom):
    """The ceiling of count times numer over denom.

    Exact here by construction. The C does this in 64 bits on purpose: 999
    times a count near four thousand million overflows a 32-bit product, and
    the rank would come out small, reporting a percentile far below the truth.
    """
    return -(-(count * numer) // denom)


def _bin_at_rank(hist, rank):
    """The bin holding the sample of that rank, counting from one.

    Returns None when the rank falls inside the overflow count, because there
    is then no bin to report and the top bin would be a wrong answer nothing
    downstream could detect.
    """
    seen = 0
    for k in range(LAT_BINS):
        seen += hist["bins"][k]
        if seen >= rank:
            return k
    return None


def report(hist):
    """The median bin, the 99.9th percentile's bin, the maximum, the overflow.

    Returns (0, dict) or (refusal, None). Each percentile carries a flag saying
    whether the histogram could answer at all.
    """
    if hist is None:
        return ERR_ARGUMENT, None
    if hist["count"] == 0:
        return ERR_EMPTY, None

    # Checked against the histogram rather than trusted: a count written
    # separately gives ranks that point at the wrong samples, and nothing in
    # the output would show it.
    if sum(hist["bins"]) + hist["over"] != hist["count"]:
        return ERR_COUNT, None

    # The rank definition is PINNED, because the chapter does not state one and
    # two reasonable choices differ by one sample. The percentile is the sample
    # of rank ceil(p * n), counting from one.
    median = _bin_at_rank(hist, _rank_of(hist["count"], 1, 2))
    p999 = _bin_at_rank(hist, _rank_of(hist["count"], 999, 1000))
    return 0, {
        "median_us": 0 if median is None else median,
        "p999_us": 0 if p999 is None else p999,
        "median_resolved": 0 if median is None else 1,
        "p999_resolved": 0 if p999 is None else 1,
        "max_us": hist["max_us"],
        "over": hist["over"],
        "count": hist["count"],
    }


def _decide_bin(bin_us, budget):
    """Bin k covers the true values from k up to just under k plus one.

    Written as a strict comparison and not as "bin plus one at or below the
    budget", which is the same thing in this language and NOT the same thing in
    the C. There the sum is a uint32_t, the maximum passes through here
    unbounded by the bin count, and a maximum of 4294967295 microseconds made
    that sum wrap to zero and reported the worst possible run as a PASS. Found
    by clippy on the Rust twin on Wednesday 28 October 2026, where the same
    expression is also a rejected lint.
    """
    if bin_us < budget:
        return LAT_PASS
    if bin_us > budget:
        return LAT_FAIL
    # bin_us == budget: part of the bin meets the budget and part does not, and
    # the truncation puts the true value at or above the recorded one.
    return LAT_UNDECIDED


def _decide_unresolved(budget):
    """An unresolved percentile is a value at or above LAT_BINS microseconds."""
    return LAT_FAIL if LAT_BINS > budget else LAT_UNDECIDED


def verdict(rep, budget):
    """The three budget rows judged. Returns (0, dict) or (refusal, None)."""
    if rep is None or budget is None:
        return ERR_ARGUMENT, None
    out = {}
    out["median"] = (_decide_bin(rep["median_us"], budget["median_us"])
                     if rep["median_resolved"]
                     else _decide_unresolved(budget["median_us"]))
    out["p999"] = (_decide_bin(rep["p999_us"], budget["p999_us"])
                   if rep["p999_resolved"]
                   else _decide_unresolved(budget["p999_us"]))
    # The maximum is a recorded value and not a bin, but the same division
    # truncated it, so it is judged the same way.
    out["max"] = _decide_bin(rep["max_us"], budget["max_us"])
    return 0, out


def stack(budget, free_bytes):
    """One task's stack, from the budget and the kernel's smallest free figure.

    Returns (0, dict) or (refusal, None).
    """
    if budget == 0:
        return ERR_STACK, None
    # More free space than the whole budget is impossible, so the two numbers
    # came from different tasks. Refused rather than clamped.
    if free_bytes > budget:
        return ERR_STACK, None

    used = budget - free_bytes
    # Eighty percent as a comparison and not a division, so the rule is exact
    # at the boundary.
    return 0, {"used": used,
               "permil": (used * 1000) // budget,
               "within": 1 if used * 5 <= budget * 4 else 0}


# ---------------------------------------------------------------------------
# The two readings chapter 20's wording invites and this project refuses.
# Nothing above calls either.

def chapter_two_way_verdict(bin_us, budget):
    """A percentile compared with its budget as though it were a value.

    Criterion 4 says the three figures "meet the budget written in the budget
    table above", which reads as a two-way comparison. The percentile is a bin
    one microsecond wide, and the budget rows are whole microseconds, so on the
    boundary bin this answers pass where part of the bin does not meet the
    budget at all. The truncation in `record` makes the error one sided: the
    true value is at or above the recorded one, so the wrong answer is always a
    pass and never a failure.
    """
    return LAT_PASS if bin_us <= budget else LAT_FAIL


def chapter_used_from_kernel(high_water):
    """The kernel's figure read as the used figure.

    Criterion 6 asks that "no task has used more than eighty percent of its
    budget" and the kernel's uxTaskGetStackHighWaterMark reports the smallest
    FREE space the task has ever had. Reading it as used reports a task at
    eighty percent as a task at twenty, which is the direction that hides the
    problem.
    """
    return high_water
