// projects/P08-node-state-machine/cpp/node_sm.hpp: the same eighteen rows, in C++17.
//
// Header only, no exceptions, no RTTI, no heap, no iostream, and nothing that
// can block. The table is the behaviour, as in the C, and the row order is the
// C's order because the parity test compares row indices rather than only the
// resulting state. Two rows share a from-state and an event, the pair of
// TX_FAIL rows in TX, and they are told apart only by their guards; the retry
// row comes first, and if it came second it would be unreachable.
//
// What C++ adds here, and it is not much, which is itself the finding: the
// table can be `constexpr` and the dispatcher can be `constexpr`, so a whole
// event sequence could in principle be evaluated at compile time. It is not,
// because the encoder it calls writes into a buffer the caller owns, and the
// value of this project is the table rather than a demonstration of constant
// evaluation. What C++ does buy is that `Row` holds function pointers to
// `constexpr` functions, so the compiler can see through the whole dispatch,
// and that the enums are scoped, so `State::Tx` and `Event::TxOk` cannot be
// mixed up the way two plain C enums can.
//
// The encoder is P09's own C++ header, not a copy. The acceptance criterion
// P08 shares with P09 is that the stub's bytes equal what P09's encoder
// produces for the same input, and including it makes that true by
// construction.
#ifndef P08_NODE_SM_HPP
#define P08_NODE_SM_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "payload.hpp"

namespace p08 {

enum class State : std::uint8_t { Init, Idle, Sense, Feature, Encode, Tx, Backoff, Fault };

enum class Event : std::uint8_t {
    Tick, Block, FeatureDone, FrameReady, TxOk, TxFail, Timeout, Button, Fault
};

// The retry limit, here rather than in a row, because a row carrying it would
// describe policy and transition together and neither would be readable.
inline constexpr std::uint32_t tx_attempt_limit = 3;

// Everything the machine knows. Flat and fixed, as the C struct is.
struct Ctx {
    State state = State::Init;
    std::uint32_t tx_attempts = 0;
    std::uint32_t cycles_ok = 0;
    std::uint32_t cycles_dropped = 0;
    std::int32_t feature = 0;
    std::uint32_t sequence = 0;
    std::int32_t pending_sample = 0;   // the seam: a caller sets this before Block
    std::array<std::uint8_t, 64> frame{};
    std::uint8_t frame_len = 0;
    std::uint32_t unhandled = 0;
};

// ------------------------------------------------------------------ actions

// Each action is a function of the context and nothing else. None blocks, none
// fails silently, none posts an event.

constexpr void act_nothing(Ctx&) {}

constexpr void act_start_cycle(Ctx& ctx)
{
    ctx.tx_attempts = 0;
    ctx.frame_len = 0;
}

constexpr void act_take_feature(Ctx& ctx)
{
    // One line: this project's subject is the table, not the signal processing.
    ctx.feature = ctx.pending_sample;
}

constexpr void act_encode(Ctx& ctx)
{
    const payload::Values in{1, 0, ctx.sequence, ctx.feature, 40};
    const payload::Buffer out = payload::encode(in);
    for (std::size_t i = 0; i < payload::total_bytes; ++i) {
        ctx.frame[i] = out[i];
    }
    ctx.frame_len = static_cast<std::uint8_t>(payload::total_bytes);
}

constexpr void act_encode_and_attempt(Ctx& ctx)
{
    // tx_attempts counts ATTEMPTS, not failures. The C got this wrong first by
    // incrementing on TxFail, which made the counter mean "failures so far"
    // while the guard read it as "attempts so far", and the limit was off by
    // one. The attempt is counted where the attempt happens.
    act_encode(ctx);
    ctx.tx_attempts = 1;
}

constexpr void act_another_attempt(Ctx& ctx) { ctx.tx_attempts++; }

constexpr void act_cycle_ok(Ctx& ctx)
{
    ctx.cycles_ok++;
    ctx.sequence = (ctx.sequence + 1) & 0x1FFu;   // wraps at 512, as P09's field does
}

constexpr void act_cycle_dropped(Ctx& ctx)
{
    // Counted, not hidden. A node that silently abandons cycles looks exactly
    // like one that is working.
    ctx.cycles_dropped++;
    ctx.sequence = (ctx.sequence + 1) & 0x1FFu;
}

// ------------------------------------------------------------------- guards

constexpr bool guard_always(const Ctx&) { return true; }
constexpr bool guard_may_retry(const Ctx& ctx) { return ctx.tx_attempts < tx_attempt_limit; }
constexpr bool guard_out_of_retries(const Ctx& ctx) { return ctx.tx_attempts >= tx_attempt_limit; }

// -------------------------------------------------------------------- table

struct Row {
    State from;
    Event event;
    bool (*guard)(const Ctx&);
    void (*action)(Ctx&);
    State to;
    const char* name;
};

inline constexpr std::array<Row, 18> table{{
    {State::Init,    Event::Tick,        guard_always,         act_start_cycle,        State::Sense,   "init: first tick starts a cycle"},
    {State::Init,    Event::Fault,       guard_always,         act_nothing,            State::Fault,   "init: fault before anything"},

    {State::Idle,    Event::Tick,        guard_always,         act_start_cycle,        State::Sense,   "idle: periodic wake"},
    {State::Idle,    Event::Button,      guard_always,         act_start_cycle,        State::Sense,   "idle: user forced a cycle"},
    {State::Idle,    Event::Fault,       guard_always,         act_nothing,            State::Fault,   "idle: fault"},

    {State::Sense,   Event::Block,       guard_always,         act_nothing,            State::Feature, "sense: a block arrived"},
    {State::Sense,   Event::Timeout,     guard_always,         act_cycle_dropped,      State::Idle,    "sense: no block in time"},
    {State::Sense,   Event::Fault,       guard_always,         act_nothing,            State::Fault,   "sense: fault"},

    {State::Feature, Event::FeatureDone, guard_always,         act_take_feature,       State::Encode,  "feature: computed"},
    {State::Feature, Event::Fault,       guard_always,         act_nothing,            State::Fault,   "feature: fault"},

    {State::Encode,  Event::FrameReady,  guard_always,         act_encode_and_attempt, State::Tx,      "encode: payload packed, first attempt"},
    {State::Encode,  Event::Fault,       guard_always,         act_nothing,            State::Fault,   "encode: fault"},

    {State::Tx,      Event::TxOk,        guard_always,         act_cycle_ok,           State::Idle,    "tx: accepted"},
    {State::Tx,      Event::TxFail,      guard_may_retry,      act_nothing,            State::Backoff, "tx: failed, will retry"},
    {State::Tx,      Event::TxFail,      guard_out_of_retries, act_cycle_dropped,      State::Idle,    "tx: failed, out of retries"},
    {State::Tx,      Event::Fault,       guard_always,         act_nothing,            State::Fault,   "tx: fault"},

    {State::Backoff, Event::Timeout,     guard_always,         act_another_attempt,    State::Tx,      "backoff: elapsed, another attempt"},
    {State::Backoff, Event::Fault,       guard_always,         act_nothing,            State::Fault,   "backoff: fault"},

    // Fault has no outgoing row on purpose. Only a reset leaves it.
}};

// --------------------------------------------------------------- dispatcher

/// Post one event. Returns the index of the row taken, or -1 when no row
/// matches, which is not an error: an event that does not apply in the current
/// state is ignored on purpose, and the count of those is worth watching.
constexpr int dispatch(Ctx& ctx, Event event)
{
    for (std::size_t i = 0; i < table.size(); ++i) {
        const Row& r = table[i];
        if (r.from != ctx.state || r.event != event) {
            continue;
        }
        if (!r.guard(ctx)) {
            continue;
        }
        // Action before the state change, so an action can read the state it is
        // leaving.
        r.action(ctx);
        ctx.state = r.to;
        return static_cast<int>(i);
    }
    ctx.unhandled++;
    return -1;
}

// The names the parity protocol uses, which are the C's node_sm_state_name and
// node_sm_event_name spellings so that one vocabulary serves all four.
constexpr const char* name(State s)
{
    switch (s) {
    case State::Init:    return "INIT";
    case State::Idle:    return "IDLE";
    case State::Sense:   return "SENSE";
    case State::Feature: return "FEATURE";
    case State::Encode:  return "ENCODE";
    case State::Tx:      return "TX";
    case State::Backoff: return "BACKOFF";
    case State::Fault:   return "FAULT";
    }
    return "?";
}

constexpr const char* name(Event e)
{
    switch (e) {
    case Event::Tick:        return "TICK";
    case Event::Block:       return "BLOCK";
    case Event::FeatureDone: return "FEATURE_DONE";
    case Event::FrameReady:  return "FRAME_READY";
    case Event::TxOk:        return "TX_OK";
    case Event::TxFail:      return "TX_FAIL";
    case Event::Timeout:     return "TIMEOUT";
    case Event::Button:      return "BUTTON";
    case Event::Fault:       return "FAULT";
    }
    return "?";
}

}  // namespace p08

#endif  // P08_NODE_SM_HPP
