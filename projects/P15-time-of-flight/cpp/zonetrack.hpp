// projects/P15-time-of-flight/cpp/zonetrack.hpp: the hysteresis of chapter 15
// figure 15.4, in C++17.
//
// The same state machine, with the same edges in the same order, as
// `../c/zonetrack.c`, `../python/zonetrack.py` and `../rust/src/lib.rs`. The
// reasoning is in `../c/zonetrack.h` and is not repeated here, including which
// source the specification comes from: step 9 gives two thresholds and the
// confirmation count, figure 15.4 gives those AND a third threshold of 1500 mm
// that the sketch omits, and the figure is the more complete of the two.
//
// WHAT C++ ADDS HERE IS THE RUST'S ANSWER, ONE STEP SHORT. The volume's list:
//
//   P18  one function template where the C needs two forty-line copies
//   P12  the C's lack of an allocator costs it sixty lines of fixed buffers,
//        bounds checks and an insertion sort
//   P17  almost nothing: four small functions over fixed-width integers
//   P16  a fourth independent algorithm for the one quantity where
//        implementations have real room to disagree
//   P20  std::optional turns finding three from a convention into a type
//   P04  std::variant reaches the same guarantee the Rust gets from an enum
//   P07  an allocator, again: a parameter pair and a refusal disappear
//   P10  std::variant, again, on a promise the C's header makes in prose
//   P11  two C conventions become types: a sentinel return and a sentinel
//        terminated array
//   P13  std::optional and a sized container, both of P11's answers at once
//   P14  the same two again, and no decision about the standard library
//   P15  AN ENUM CLASS REMOVES THE REFUSAL FOR AN IMPOSSIBLE STATE, and the
//        compiler warns about a missing arm rather than refusing the file
//
// THE DEFAULT ARM, AND WHY THIS IS ONE STEP SHORT OF THE RUST. The C's switch
// ends with `default: return ZONETRACK_ERR_ARGUMENT;`, because its state is a
// plain enum in a struct and any int fits there. An `enum class` here is still
// an enum with an underlying type, so a value outside the four is still
// REPRESENTABLE by a cast or a memcpy; what changes is that nothing in this
// file produces one, and -Wswitch, which this repository compiles with as an
// error through -Wall, refuses a switch that omits a known enumerator. So the
// default arm is gone and a fifth state added later is a compile error at every
// switch.
//
// The Rust's `match` is exhaustive over the four variants as a language rule
// and its enum cannot hold a fifth value at all, so there the refusal is
// unreachable rather than merely unproduced. That difference is small and it is
// real, and writing it down is the point of asking the question once per
// project: this is the first entry in the list where C++ gets most of the
// Rust's answer and the remainder is nameable.
//
// EVERY QUANTITY HERE IS AN INTEGER, so there is no floating point, no
// contraction flag and no std-against-core question to answer.
#ifndef P15_ZONETRACK_HPP
#define P15_ZONETRACK_HPP

#include <cstdint>
#include <optional>

namespace p15 {

// The window is a bitmask, so the frames it can hold is the bits in it.
constexpr std::uint8_t kWindowMax = 16;

// A frame with no valid zone at all, further away than any threshold.
constexpr std::uint16_t kNoTarget = 0xFFFF;

// The four states, numbered as chapter 15's host decoder indexes them:
// ["clear", "approaching", "present", "leaving"][(b >> 5) & 3]. THE NUMBERING
// IS PART OF THE WIRE FORMAT and not an internal detail.
enum class State : std::uint8_t {
    kClear = 0,
    kApproaching = 1,
    kPresent = 2,
    kLeaving = 3,
};

inline const char *name(State s)
{
    switch (s) {
    case State::kClear:       return "clear";
    case State::kApproaching: return "approaching";
    case State::kPresent:     return "present";
    case State::kLeaving:     return "leaving";
    }
    // Unreachable for a value this file produces, and kept because a cast or a
    // memcpy can still put a fifth value in an enum class. The C's version of
    // this function returns the same word for the same reason.
    return "invalid";
}

// The same two refusals as the C, with its names.
enum class Error {
    kArgument = 1,  // the C's null tracker, null policy or impossible state
    kPolicy,
};

inline const char *error_name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kPolicy:   return "policy";
    }
    return "unknown";
}

// The policy, passed rather than compiled in, because the chapter's own note on
// figure 15.4 calls its numbers "a starting point measured against a real
// target rather than constants to be trusted".
struct Policy {
    std::uint16_t far_mm = 0;    // the gate between clear and approaching
    std::uint16_t enter_mm = 0;  // approaching to present, with confirmation
    std::uint16_t leave_mm = 0;  // present to leaving, with confirmation
    std::uint8_t confirm_n = 0;  // agreeing frames needed
    std::uint8_t confirm_m = 0;  // out of how many, the window
};

inline bool policy_sane(const Policy &p)
{
    if (p.confirm_m == 0u || p.confirm_m > kWindowMax) {
        return false;
    }
    if (p.confirm_n == 0u || p.confirm_n > p.confirm_m) {
        return false;
    }
    // Enter is nearer than leave, and leave is nearer than far. A policy with
    // leave below enter has no hysteresis at all: the two thresholds would
    // cross and the state would chatter at the crossing, which is the single
    // thing this whole file exists to prevent.
    return p.enter_mm < p.leave_mm && p.leave_mm < p.far_mm;
}

struct Tracker {
    State state = State::kClear;
    std::uint16_t votes = 0;   // one bit per frame, newest at bit 0
    std::uint8_t seen = 0;     // frames in the window, saturating at confirm_m
    std::uint32_t frames = 0;  // every frame this tracker has been given
    std::uint32_t changes = 0; // the figure a policy is judged by
    bool ready = false;        // a full window has been seen at least once
};

// Push one vote into the window and report how many of the last confirm_m
// frames voted yes.
//
// The C counts the bits in a loop and the Rust calls count_ones. THIS KEEPS
// THE C'S LOOP, because std::popcount is C++20 and this file is C++17, and the
// alternative was a __builtin_popcount with a fallback branch that nothing in
// this repository would ever compile. An untested branch in a header is worse
// than a loop over at most sixteen bits. Only the count reaches the
// comparison, so none of this is in the specification.
inline std::uint32_t push_vote(Tracker &t, const Policy &p, bool yes)
{
    const std::uint32_t keep = (p.confirm_m >= 32u)
                             ? 0xFFFFFFFFu
                             : ((1u << p.confirm_m) - 1u);
    const std::uint32_t window =
        ((static_cast<std::uint32_t>(t.votes) << 1) | (yes ? 1u : 0u)) & keep;
    t.votes = static_cast<std::uint16_t>(window);

    if (t.seen < p.confirm_m) {
        t.seen++;
        if (t.seen == p.confirm_m) {
            t.ready = true;
        }
    }

    std::uint32_t count = 0u;
    const std::uint32_t width = p.confirm_m;
    for (std::uint32_t bit = 0u; bit < width; bit++) {
        if ((window & (1u << bit)) != 0u) {
            count++;
        }
    }
    return count;
}

inline void enter(Tracker &t, State next)
{
    if (next != t.state) {
        t.state = next;
        t.changes++;
    }
    // Cleared on every change, including a change into the same state, which
    // cannot happen here but would be the quiet kind of defect if it could.
    t.votes = 0u;
    t.seen = 0u;
}

// Feed one frame's nearest valid distance in millimetres and get the state
// after it, or a refusal.
inline std::optional<Error> update(Tracker &t, const Policy &p,
                                   std::uint16_t nearest_mm, State &out)
{
    if (!policy_sane(p)) {
        return Error::kPolicy;
    }
    t.frames++;

    switch (t.state) {
    case State::kClear:
        // Immediate, with no confirmation: the figure draws no count on this
        // edge, and a node that waited three frames to notice something is in
        // the room would be slower than the sensor.
        if (nearest_mm < p.far_mm) {
            enter(t, State::kApproaching);
        }
        break;

    case State::kApproaching:
        if (push_vote(t, p, nearest_mm < p.enter_mm)
            >= static_cast<std::uint32_t>(p.confirm_n)) {
            enter(t, State::kPresent);
        } else if (nearest_mm >= p.far_mm) {
            // Went away again without ever confirming. The figure has no edge
            // for this, and leaving the node in approaching for ever after a
            // target walks out of range would be worse.
            enter(t, State::kClear);
        }
        break;

    case State::kPresent:
        if (push_vote(t, p, nearest_mm > p.leave_mm)
            >= static_cast<std::uint32_t>(p.confirm_n)) {
            enter(t, State::kLeaving);
        }
        break;

    case State::kLeaving:
        if (nearest_mm > p.far_mm) {
            enter(t, State::kClear);
        } else if (nearest_mm < p.enter_mm) {
            // Came back. The figure has no edge for this either, and a target
            // that returns while the node still calls it leaving should be
            // present again rather than waiting to pass 1500 mm first.
            enter(t, State::kPresent);
        }
        break;
    }
    // No default arm: -Wswitch refuses a switch that omits an enumerator, so a
    // fifth state added later is a compile error here rather than a silent
    // fall-through. The header comment says how that differs from the Rust.

    out = t.state;
    return std::nullopt;
}

}  // namespace p15

#endif  // P15_ZONETRACK_HPP
