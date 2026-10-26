"""P15's hysteresis in Python, the twin of c/zonetrack.c.

The same four states, the same four edges from figure 15.4, the same two
decisions the figure does not draw, and the same refusals. It exists so the
oracle is driven by two implementations rather than by the one it was written
from.
"""
from __future__ import annotations

CLEAR, APPROACHING, PRESENT, LEAVING = 0, 1, 2, 3
STATE_NAMES = ("clear", "approaching", "present", "leaving")

WINDOW_MAX = 16
NO_TARGET = 0xFFFF

ERR_ARGUMENT = -1
ERR_POLICY = -2
ERRORS = {"argument": ERR_ARGUMENT, "policy": ERR_POLICY}


def policy_sane(p: dict) -> bool:
    if not 1 <= p["confirm_m"] <= WINDOW_MAX:
        return False
    if not 1 <= p["confirm_n"] <= p["confirm_m"]:
        return False
    return p["enter_mm"] < p["leave_mm"] < p["far_mm"]


def fresh_tracker() -> dict:
    return {"state": CLEAR, "votes": 0, "seen": 0,
            "frames": 0, "changes": 0, "ready": False}


def _push_vote(t: dict, p: dict, yes: bool) -> int:
    keep = (1 << p["confirm_m"]) - 1
    t["votes"] = ((t["votes"] << 1) | (1 if yes else 0)) & keep
    if t["seen"] < p["confirm_m"]:
        t["seen"] += 1
        if t["seen"] == p["confirm_m"]:
            t["ready"] = True
    return bin(t["votes"]).count("1")


def _enter(t: dict, nxt: int) -> None:
    if nxt != t["state"]:
        t["state"] = nxt
        t["changes"] += 1
    t["votes"] = 0
    t["seen"] = 0


def update(t: dict, p: dict, nearest_mm: int) -> int:
    if t is None or p is None:
        return ERR_ARGUMENT
    if not policy_sane(p):
        return ERR_POLICY

    t["frames"] += 1
    state = t["state"]

    if state == CLEAR:
        if nearest_mm < p["far_mm"]:
            _enter(t, APPROACHING)
    elif state == APPROACHING:
        if _push_vote(t, p, nearest_mm < p["enter_mm"]) >= p["confirm_n"]:
            _enter(t, PRESENT)
        elif nearest_mm >= p["far_mm"]:
            _enter(t, CLEAR)
    elif state == PRESENT:
        if _push_vote(t, p, nearest_mm > p["leave_mm"]) >= p["confirm_n"]:
            _enter(t, LEAVING)
    elif state == LEAVING:
        if nearest_mm > p["far_mm"]:
            _enter(t, CLEAR)
        elif nearest_mm < p["enter_mm"]:
            _enter(t, PRESENT)
    else:
        return ERR_ARGUMENT

    return t["state"]


def state_name(state: int) -> str:
    if 0 <= state < len(STATE_NAMES):
        return STATE_NAMES[state]
    return "invalid"
