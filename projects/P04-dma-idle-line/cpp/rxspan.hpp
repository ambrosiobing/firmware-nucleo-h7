// projects/P04-dma-idle-line/cpp/rxspan.hpp: the span arithmetic in C++17.
//
// The same arithmetic, in the same order, as `../c/rxspan.c`,
// `../python/rxspan.py` and `../rust/src/lib.rs`. Chapter 4's receive path has
// no interrupt per byte: a transfer engine writes into a circular buffer and
// the processor learns where it got to by reading the transfer counter, so the
// whole of the difficult part is working out which bytes are new, as at most
// two spans neither of which crosses the end of the array. The file
// `../c/rxspan.h` carries the reasoning, including the one that matters most:
// THE GUARD IS NOT A DETECTOR, because a full lap and no movement both arrive
// as pos == old_pos.
//
// WHAT C++ ADDS HERE, AND IT IS A SIXTH DISTINCT ANSWER. The answer varies by
// project in this volume and keeping it accurate is the point of asking:
//
//   P18  one function template where the C needs two forty-line copies
//   P12  the C's lack of an allocator costs it sixty lines of fixed buffers,
//        bounds checks and an insertion sort
//   P17  almost nothing: four small functions over fixed-width integers
//   P16  a fourth independent algorithm for the one quantity where
//        implementations have real room to disagree
//   P20  std::optional turns finding three from a convention into a type
//   P04  std::variant reaches the SAME guarantee the Rust gets from an enum,
//        and charges more for it
//
// THE GUARANTEE, AND WHAT IT COSTS IN THIS LANGUAGE. The C returns an int of 0,
// 1 or 2 and writes that many entries into out[RXSPAN_MAX]. Those two can
// disagree, and a caller who reads out[1] when the count is 1 gets whatever was
// in that slot before. The Rust answers with an enum, Nothing or One or Two, so
// there is no count to disagree with and no slot to read that is not there.
//
// std::variant<std::monostate, Span, Pair> is that same sum type, exactly, and
// std::visit over it is checked for exhaustiveness at compile time the way a
// Rust match is: an overload set missing an alternative does not compile. So
// the guarantee is equal.
//
// What is not equal is the ceremony. Rust writes one `match` with three arms.
// Here a reader needs either std::visit with an overload set, which is a
// struct or a lambda pack and two more lines of scaffolding, or a sequence of
// std::get_if checks, which is exhaustive only by the author's discipline and
// so gives back the thing the variant was for. This file uses std::visit at
// the one place that consumes the answer and get_if nowhere, because a
// guarantee kept in one place and given up in another is worth less than
// either.
//
// **That is the honest shape of the comparison**: C++ can express it, the
// expression is verified by the compiler, and it is longer. Saying C++ cannot
// do it would be wrong, and saying the two are equivalent would skip the cost.
//
// EVERY QUANTITY HERE IS AN INTEGER, so there is no floating point and no
// contraction flag applies, which is why the build passes none.
#ifndef P04_RXSPAN_HPP
#define P04_RXSPAN_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace p04 {

// At most two, because a circular buffer wraps at most once between two reads
// of the position.
constexpr std::size_t kMaxSpans = 2;

// A floor on sense rather than on correctness: the near-lap guard compares the
// advance against half the buffer, and half of four bytes makes it meaningless
// rather than wrong.
constexpr std::uint32_t kMinSize = 8;

// The same three refusals as the C, in the same order.
enum class Error {
    kArgument = 1,
    kSize,
    kPosition,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kSize:     return "size";
    case Error::kPosition: return "position";
    }
    return "unknown";
}

// An offset into the buffer and a length, where off + len never exceeds the
// size, which is the plain pointer and length the chapter promises a consumer.
struct Span {
    std::uint16_t off;
    std::uint16_t len;
};

using Pair = std::pair<Span, Span>;

// What one step found. There is no count beside an array to disagree with it,
// and std::visit over this is exhaustiveness checked at compile time.
using Delivery = std::variant<std::monostate, Span, Pair>;

// The bound and the report the chapter offers in place of an overrun flag,
// because this path has none.
struct Stats {
    std::uint16_t peak_advance = 0;
    std::uint32_t near_lap = 0;
    std::uint32_t calls = 0;
    std::uint32_t deliveries = 0;
};

// The C's return value, derived from the shape rather than stored beside it.
inline std::size_t count_of(const Delivery &d)
{
    return std::visit(
        [](const auto &held) -> std::size_t {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::monostate>) {
                return 0;
            } else if constexpr (std::is_same_v<Held, Span>) {
                return 1;
            } else {
                return 2;
            }
        },
        d);
}

// One step. stats is updated on every call that is not refused, including
// calls that find nothing, because the count of calls is what the chapter's
// half and full transfer interrupts exist to keep above a floor.
inline std::optional<Error> next_spans(std::uint16_t size,
                                       std::uint16_t old_pos,
                                       std::uint16_t pos, Stats &stats,
                                       Delivery &out)
{
    const std::uint32_t n = size;
    // A power of two, and the refusal is load bearing rather than decorative:
    // the advance is a mask, and masking with n - 1 is the advance only when
    // the size is a power of two. A buffer of 500 bytes would produce a
    // plausible wrong number rather than an error.
    if (n < kMinSize || (n & (n - 1u)) != 0u) {
        return Error::kSize;
    }
    if (static_cast<std::uint32_t>(old_pos) >= n
        || static_cast<std::uint32_t>(pos) >= n) {
        return Error::kPosition;
    }

    // Masked and not subtracted, in 32 bits so the intermediate cannot wrap a
    // uint16_t on the way. The Rust spells the same step wrapping_sub because
    // its checks would otherwise stop it; C and C++ define unsigned wraparound
    // and say nothing, which is the difference that crate's README records.
    const std::uint32_t advance =
        (static_cast<std::uint32_t>(pos) - static_cast<std::uint32_t>(old_pos))
        & (n - 1u);

    stats.calls += 1;
    if (advance > static_cast<std::uint32_t>(stats.peak_advance)) {
        stats.peak_advance = static_cast<std::uint16_t>(advance);
    }
    if (advance > (n / 2u)) {
        stats.near_lap += 1;
    }

    if (pos == old_pos) {
        out = std::monostate{};
        return std::nullopt;
    }

    if (pos > old_pos) {
        out = Span{old_pos, static_cast<std::uint16_t>(pos - old_pos)};
    } else {
        const Span tail{old_pos,
                        static_cast<std::uint16_t>(n - old_pos)};
        // Wrapping exactly onto zero is ONE span and not two, because leaving
        // a zero length span out is what keeps it out of a consumer's hands.
        if (pos > 0u) {
            out = Pair{tail, Span{0u, pos}};
        } else {
            out = tail;
        }
    }
    stats.deliveries += static_cast<std::uint32_t>(count_of(out));
    return std::nullopt;
}

}  // namespace p04

#endif  // P04_RXSPAN_HPP
