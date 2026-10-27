"""P16's window features and bounded tree in Python, the twin of c/features.c.

Chapter 16's criterion 1 is an equivalence gate: all implementations produce
identical features and identical classes, and the build fails if they do not.
This is one side of it. Standard library only.

**The integer square root here is `math.isqrt` and in the C it is a bit-by-bit
descent.** That is deliberate rather than lazy: two different algorithms for the
same floored quantity is what makes the gate worth running, where two
transcriptions of one algorithm would mostly prove that the transcription was
faithful.

It also carries, at the bottom, the rounding the chapter's definition permits and
this one refuses. `chapter_mean_nearest` is a mean rounded to the nearest value,
which step 3 neither requires nor forbids, and a test runs it to show the gate
failing on a window where it differs by one least significant bit.
"""
from __future__ import annotations

import math

FEAT_WIN_N = 128
FEAT_AXES = 3
FEAT_GROUPS = 3
FEAT_COUNT = FEAT_GROUPS * FEAT_AXES
FEAT_TREE_MAX = 64

ERR_ARGUMENT = -1
ERR_SHAPE = -2
ERR_TREE = -3
ERR_NODE = -4
ERR_FEATURE = -5
ERR_DEPTH = -6

ERRORS = {"argument": ERR_ARGUMENT, "shape": ERR_SHAPE, "tree": ERR_TREE,
          "node": ERR_NODE, "feature": ERR_FEATURE, "depth": ERR_DEPTH}


def window(samples, n, axes):
    """Features for one window. Returns (0, dict) or (refusal, None).

    samples is interleaved by axis, the order the sensor's buffer delivers.
    """
    if samples is None:
        return ERR_ARGUMENT, None
    if n == 0:
        return ERR_ARGUMENT, None
    if n != FEAT_WIN_N or axes != FEAT_AXES:
        return ERR_SHAPE, None
    if len(samples) < n * axes:
        return ERR_SHAPE, None

    mean, rms, p2p = [], [], []
    for a in range(FEAT_AXES):
        column = [samples[i * FEAT_AXES + a] for i in range(FEAT_WIN_N)]
        total = sum(column)
        squares = sum(v * v for v in column)

        # Python's // already floors toward negative infinity, which is the
        # direction the C spells out by hand.
        mean.append(total >> 7)
        rms.append(math.isqrt(squares >> 7))
        span = max(column) - min(column)
        p2p.append(32767 if span > 32767 else span)

    return 0, {"mean": mean, "rms": rms, "p2p": p2p}


def vector(feat):
    """The nine features flat, in the order a tree's indices refer to."""
    if feat is None:
        return ERR_ARGUMENT, None
    return 0, list(feat["mean"]) + list(feat["rms"]) + list(feat["p2p"])


def classify(nodes, depth_max, features):
    """Walk the tree. Returns (0, class) or (refusal, None)."""
    if nodes is None or features is None:
        return ERR_ARGUMENT, None
    if len(nodes) == 0 or len(nodes) > FEAT_TREE_MAX:
        return ERR_TREE, None
    if depth_max == 0:
        return ERR_DEPTH, None
    if len(features) == 0:
        return ERR_FEATURE, None

    at = 0
    for _ in range(depth_max + 1):
        if at >= len(nodes):
            return ERR_NODE, None
        node = nodes[at]
        if node["leaf"]:
            return 0, node["feature"]
        if node["feature"] >= len(features):
            return ERR_FEATURE, None
        # At or below the threshold goes left. The boundary is stated because it
        # is exactly where two implementations of one tree disagree.
        at = node["left"] if features[node["feature"]] <= node["threshold"] \
            else node["right"]
    return ERR_DEPTH, None


# ---------------------------------------------------------------------------
# The rounding chapter 16 permits and this project refuses. Nothing above calls
# it.

def chapter_mean_nearest(column):
    """A mean rounded to the nearest value, over 128 samples.

    Step 3 fixes the window, the axes, the feature list and the numeric format,
    and says nothing about rounding. This obeys that definition exactly as
    written and disagrees with the floored mean by one least significant bit
    whenever the remainder is at least half, which is most windows. Step 4's
    gate demands equality, so two implementations each following the printed
    definition can fail it. That is why every rounding in this project is
    pinned.
    """
    total = sum(column)
    n = len(column)
    return (total + n // 2) // n
