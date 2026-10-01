"""P08's transition table: every row reachable, and the frame matching P09's codec.

Two acceptance criteria, and this file is both of them.

  1. Every row of the table is proven reachable by a test. Not "the states are
     covered" and not "the happy path works": every row, with its hit count, and
     the test fails naming any row that was never taken. A row nobody can reach is
     either dead behaviour or a design that cannot do what the table says.

  2. The stub payload equals what P09's encoder produces for the same input, byte
     for byte. That is P09's acceptance criterion as much as P08's, and it is the
     one place in this volume where two projects verify each other. It holds by
     construction rather than by agreement, because the action calls
     payload_encode rather than reimplementing it, and this test is what makes
     sure nobody later decides to reimplement it.

No hardware, no peripherals, no allocation. The state machine is the one piece of
application logic here that can be proven completely on a host, which is why it
was worth writing before the board exists.
"""
from __future__ import annotations

import ctypes

import pytest
from conftest import BUILD, CPayload, shared_library_name

# The states and events, in the order c/P08/node_sm.h declares them.
S_INIT, S_IDLE, S_SENSE, S_FEATURE, S_ENCODE, S_TX, S_BACKOFF, S_FAULT = range(8)
(E_TICK, E_BLOCK, E_FEATURE_DONE, E_FRAME_READY, E_TX_OK, E_TX_FAIL,
 E_TIMEOUT, E_BUTTON, E_FAULT) = range(9)

ATTEMPT_LIMIT = 3


class Ctx(ctypes.Structure):
    """Must match node_ctx_t in c/P08/node_sm.h, field for field and in order."""

    _fields_ = [
        ("state", ctypes.c_int),
        ("tx_attempts", ctypes.c_uint32),
        ("cycles_ok", ctypes.c_uint32),
        ("cycles_dropped", ctypes.c_uint32),
        ("feature", ctypes.c_int32),
        ("sequence", ctypes.c_uint32),
        ("pending_sample", ctypes.c_int32),
        ("frame", ctypes.c_uint8 * 64),
        ("frame_len", ctypes.c_uint8),
        ("tx_should_fail", ctypes.c_bool),
    ]


_LIB = None


def lib():
    global _LIB
    if _LIB is None:
        path = BUILD / shared_library_name("node_sm")
        if not path.exists():
            pytest.fail("{} is missing. Run: python python/tools/build_host.py"
                        .format(path.name))
        d = ctypes.CDLL(str(path))
        d.node_sm_init.argtypes = [ctypes.POINTER(Ctx)]
        d.node_sm_dispatch.restype = ctypes.c_int
        d.node_sm_dispatch.argtypes = [ctypes.POINTER(Ctx), ctypes.c_int]
        d.node_sm_row_count.restype = ctypes.c_size_t
        d.node_sm_row_hits.restype = ctypes.c_uint32
        d.node_sm_row_hits.argtypes = [ctypes.c_size_t]
        d.node_sm_row_name.restype = ctypes.c_char_p
        d.node_sm_row_name.argtypes = [ctypes.c_size_t]
        d.node_sm_unhandled.restype = ctypes.c_uint32
        d.node_sm_state_name.restype = ctypes.c_char_p
        d.node_sm_state_name.argtypes = [ctypes.c_int]
        # P09's encoder, linked into the same library, which is the point.
        d.payload_encode.restype = ctypes.c_size_t
        d.payload_encode.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                     ctypes.POINTER(CPayload)]
        _LIB = d
    return _LIB


def fresh() -> Ctx:
    c = Ctx()
    lib().node_sm_init(ctypes.byref(c))
    return c


def post(c: Ctx, event: int) -> int:
    return lib().node_sm_dispatch(ctypes.byref(c), event)


def state_name(s: int) -> str:
    return lib().node_sm_state_name(s).decode()


def one_good_cycle(c: Ctx, sample: int = 100) -> None:
    """INIT or IDLE through to IDLE again, the whole happy path."""
    post(c, E_TICK)
    c.pending_sample = sample
    post(c, E_BLOCK)
    post(c, E_FEATURE_DONE)
    post(c, E_FRAME_READY)
    post(c, E_TX_OK)


# --------------------------------------------- criterion 2: the codec agreement

def test_the_stub_payload_matches_P09s_encoder_byte_for_byte():
    """P08's and P09's shared acceptance criterion, and the only cross-check here.

    The state machine's encode action calls payload_encode, so this holds by
    construction. The test exists so that it keeps holding: a later decision to
    reimplement the packing inside the state machine would fail here rather than
    silently producing a second layout.
    """
    for sample in (0, 1, -1, 100, -100, 131071, -131072, 65535):
        c = fresh()
        one_good_cycle(c, sample)
        from_sm = bytes(c.frame[:c.frame_len])

        expect = (ctypes.c_uint8 * 8)()
        n = lib().payload_encode(expect, 8, ctypes.byref(CPayload(
            version=1, flags=0, sequence=0, feature=sample, battery=40)))

        assert c.frame_len == 5, "the frame is not five bytes for sample {}".format(sample)
        assert from_sm == bytes(expect[:n]), (
            "sample {}: the state machine produced {} and P09's encoder {}".format(
                sample, from_sm.hex().upper(), bytes(expect[:n]).hex().upper()))


def test_the_frame_is_exactly_the_bytes_a_radio_would_have_been_handed():
    """The stub records the attempt and does not touch the frame.

    A stub that rewrote or padded what it was given would make this project look
    finished and leave P09's codec untested from this side.
    """
    c = fresh()
    one_good_cycle(c, 1234)
    before = bytes(c.frame[:c.frame_len])
    # The transmit attempt happens on failure in this table; drive one and check.
    c2 = fresh()
    post(c2, E_TICK); c2.pending_sample = 1234
    post(c2, E_BLOCK); post(c2, E_FEATURE_DONE); post(c2, E_FRAME_READY)
    frame_before_tx = bytes(c2.frame[:c2.frame_len])
    post(c2, E_TX_FAIL)
    assert bytes(c2.frame[:c2.frame_len]) == frame_before_tx
    assert frame_before_tx == before


# ----------------------------------------- criterion 1: every row is reachable

def test_every_transition_row_is_reachable():
    """The headline criterion. Drives sequences, then names any row never taken.

    The failure message lists the rows by their own names, so a zero identifies
    itself rather than leaving somebody to count table positions.
    """
    lib().node_sm_reset_hits()

    # The happy path, twice, which also exercises the sequence wrap bookkeeping.
    c = fresh()
    one_good_cycle(c)
    one_good_cycle(c)

    # The button, from IDLE.
    post(c, E_BUTTON); c.pending_sample = 7
    post(c, E_BLOCK); post(c, E_FEATURE_DONE); post(c, E_FRAME_READY); post(c, E_TX_OK)

    # A sense timeout.
    post(c, E_TICK)
    post(c, E_TIMEOUT)

    # Transmit failure with retries left, then the backoff, then success.
    post(c, E_TICK); c.pending_sample = 1
    post(c, E_BLOCK); post(c, E_FEATURE_DONE); post(c, E_FRAME_READY)
    post(c, E_TX_FAIL)        # retry row
    post(c, E_TIMEOUT)        # backoff elapsed
    post(c, E_TX_OK)

    # Transmit failure until the retry limit, which takes the other TX_FAIL row.
    post(c, E_TICK); c.pending_sample = 2
    post(c, E_BLOCK); post(c, E_FEATURE_DONE); post(c, E_FRAME_READY)
    for _ in range(ATTEMPT_LIMIT):
        post(c, E_TX_FAIL)
        if c.state == S_BACKOFF:
            post(c, E_TIMEOUT)
    assert c.state == S_IDLE, "the retry limit should return to IDLE"

    # A fault from every state that has a fault row.
    for reach in (
        lambda x: None,                                             # INIT
        lambda x: one_good_cycle(x),                                 # IDLE
        lambda x: (post(x, E_TICK),),                                # SENSE
        lambda x: (post(x, E_TICK), setattr(x, "pending_sample", 3),
                   post(x, E_BLOCK)),                               # FEATURE
        lambda x: (post(x, E_TICK), setattr(x, "pending_sample", 3),
                   post(x, E_BLOCK), post(x, E_FEATURE_DONE)),      # ENCODE
        lambda x: (post(x, E_TICK), setattr(x, "pending_sample", 3),
                   post(x, E_BLOCK), post(x, E_FEATURE_DONE),
                   post(x, E_FRAME_READY)),                         # TX
        lambda x: (post(x, E_TICK), setattr(x, "pending_sample", 3),
                   post(x, E_BLOCK), post(x, E_FEATURE_DONE),
                   post(x, E_FRAME_READY), post(x, E_TX_FAIL)),     # BACKOFF
    ):
        f = fresh()
        reach(f)
        post(f, E_FAULT)
        assert f.state == S_FAULT, "fault from {} did not reach FAULT".format(
            state_name(f.state))

    # Now the criterion itself.
    n = lib().node_sm_row_count()
    never = [(i, lib().node_sm_row_name(i).decode())
             for i in range(n) if lib().node_sm_row_hits(i) == 0]
    assert not never, "rows never taken: {}".format(never)
    print("all {} rows reachable; unhandled events posted: {}".format(
        n, lib().node_sm_unhandled()))


def test_the_fault_state_has_no_way_out():
    """Only a reset leaves FAULT, and every event posted there is unhandled.

    A fault state with an exit nobody designed is how a board comes back to life in
    an unknown condition. This asserts the absence of that exit rather than
    trusting the table to have omitted it.
    """
    for event in (E_TICK, E_BLOCK, E_FEATURE_DONE, E_FRAME_READY,
                  E_TX_OK, E_TX_FAIL, E_TIMEOUT, E_BUTTON, E_FAULT):
        c = fresh()
        post(c, E_FAULT)
        assert c.state == S_FAULT
        row = post(c, event)
        assert row == -1, "event {} found a way out of FAULT".format(event)
        assert c.state == S_FAULT


def test_an_event_that_does_not_apply_is_ignored_and_counted():
    """Ignoring is deliberate; counting is what makes it checkable."""
    lib().node_sm_reset_hits()
    c = fresh()
    before = lib().node_sm_unhandled()
    assert post(c, E_TX_OK) == -1, "TX_OK should not apply in INIT"
    assert c.state == S_INIT, "an unhandled event must not change the state"
    assert lib().node_sm_unhandled() == before + 1


def test_the_retry_limit_is_enforced_and_the_drop_is_counted():
    c = fresh()
    post(c, E_TICK); c.pending_sample = 5
    post(c, E_BLOCK); post(c, E_FEATURE_DONE); post(c, E_FRAME_READY)
    for _ in range(ATTEMPT_LIMIT):
        post(c, E_TX_FAIL)
        if c.state == S_BACKOFF:
            post(c, E_TIMEOUT)
    assert c.state == S_IDLE
    assert c.cycles_dropped == 1, "a cycle abandoned after the limit must be counted"
    assert c.cycles_ok == 0


def test_the_sequence_wraps_at_512_as_P09s_field_does():
    """The field is nine bits. A sequence that walked past 511 would encode as
    something else and the two projects would disagree about the payload."""
    c = fresh()
    for _ in range(515):
        one_good_cycle(c)
    assert c.cycles_ok == 515
    assert c.sequence == 515 % 512


def test_a_dropped_cycle_also_advances_the_sequence():
    """Otherwise a receiver cannot tell a lost frame from a repeated one."""
    c = fresh()
    post(c, E_TICK)
    post(c, E_TIMEOUT)                 # the cycle is abandoned at sense
    assert c.cycles_dropped == 1
    assert c.sequence == 1, "an abandoned cycle must still consume its sequence number"
