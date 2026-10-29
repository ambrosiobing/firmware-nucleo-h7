// projects/P13-iks4a1-fifo/cpp/fifodecode.hpp: the buffer decode, the drop
// count in the counter's own modulus and the fill-to-drain ratio, in C++17.
//
// The same arithmetic, in the same order, as `../c/fifodecode.c`,
// `../python/fifodecode.py` and `../rust/src/lib.rs`. The four findings this
// project is about are written out in `../c/fifodecode.h` and are not repeated
// here: the chapter's drop count is computed in the wrong modulus, it
// fabricates drops at startup, a gap of exactly the counter's modulus is
// invisible, and the fill time divides by one source's rate while the buffer
// holds several.
//
// WHAT C++ ADDS HERE IS WHAT IT ADDED TO P20 AND TO P11, AND THE REPETITION IS
// THE POINT RATHER THAN A SHORTFALL. The volume's answers so far:
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
//   P13  std::optional AND a sized container, so BOTH of P11's answers at once,
//        and this time the optional carries a FINDING rather than a refusal
//
// Nine projects in, the list has stopped producing new answers and started
// producing the same three: a template where the C repeats itself, a container
// where the C passes a pointer and a count, and an optional where the C uses a
// sentinel or a flag. **That is a fact about the C idiom rather than about
// C++.** These chapters are written in the style this part is programmed in, so
// same three shapes recur, and a list that kept producing novelties would be a
// list of this author reaching for novelty.
//
// FINDING TWO IN THE TYPE SYSTEM. The C's carried state holds two fields for
// one fact:
//
//     uint8_t expect_cnt;  /* carried between drains */
//     uint8_t started;     /* 0 until the first word is seen */
//
// The flag exists BECAUSE of finding two: the chapter has no `started` at all,
// its expectation is a static beginning at zero, so the first word of the first
// drain is measured against a zero nobody wrote. Adding the flag is the right
// fix. But the pair can disagree, and nothing in the type prevents it. Here it
// is `std::optional<std::uint8_t>`, where an empty optional IS "no word seen
// yet", and the comparison cannot be written without taking the expectation out
// of the option first. The Rust reaches the same place with `Option<u8>`.
//
// AND THE CONTAINER CARRIES ITS LENGTH, which is about memory rather than
// arithmetic. The C takes `const uint8_t *buf` with a separate `words`, and its
// header says "words is the buffer level already clamped by the caller", so a
// level larger than the buffer reads past the end: undefined behaviour rather
// than a wrong answer. Here the block is a `std::vector<std::uint8_t>` and a
// buffer too short for the stated word count is `Error::kArgument`. **No oracle
// case reaches that**, so it is not a new case the C would have to answer; it
// is the same refusal name for a condition the C cannot detect. C++20 would
// spell
// the argument `std::span`; this is C++17, so it is a const reference.
//
// THE BUDGET IS FLOATING POINT, SO THE BUILD PASSES -ffp-contract=off. gcc at
// -O2 fuses a multiply followed by an add, rounding once where two operations
// round twice, and the drain time is exactly that shape. The oracle compares
// these figures to a relative 1e-12 rather than exactly, so a fused result
// would probably pass, and "probably passes" is not a reason to let a build
// differ from its twins. python/tests/test_float_builds_disable_contraction.py
// requires the flag, reading this header as well as the source.
#ifndef P13_FIFODECODE_HPP
#define P13_FIFODECODE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace p13 {

// A five-bit tag field is the widest this decode tabulates, so the counts fit a
// fixed array and an unknown tag is a count rather than an index into nothing.
constexpr std::size_t kTagValues = 32;
constexpr std::uint8_t kMaxWordBytes = 16;
constexpr std::uint16_t kMaxWords = 1024;
constexpr std::size_t kMaxSources = 8;

// The same five refusals as the C, in the same order and with its names.
enum class Error {
    kArgument = 1,  // a null pointer there; here, a block too short
    kFormat,
    kWords,
    kRate,
    kSources,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kFormat:   return "format";
    case Error::kWords:    return "words";
    case Error::kRate:     return "rate";
    case Error::kSources:  return "sources";
    }
    return "unknown";
}

// The word layout, a parameter and not a constant: the chapter states only the
// word shape, a tag byte and six data bytes, and sends the tag format to each
// sensor's own datasheet. Every finding holds for any counter narrower than a
// byte.
struct Format {
    std::uint8_t word_bytes = 0;
    std::uint8_t tag_shift = 0;
    std::uint8_t cnt_shift = 0;
    std::uint8_t cnt_bits = 0;
    std::uint32_t known_tags = 0;
};

inline bool format_ok(const Format &fmt)
{
    if (fmt.word_bytes == 0u || fmt.word_bytes > kMaxWordBytes) {
        return false;
    }
    if (fmt.cnt_bits == 0u || fmt.cnt_bits > 8u) {
        return false;
    }
    if (fmt.tag_shift > 7u || fmt.cnt_shift > 7u) {
        return false;
    }
    // The tag field is whatever remains above tag_shift, so a shift leaving
    // room for more values than the table has is a format this cannot tabulate
    // rather than one to silently truncate.
    return (0xFFu >> fmt.tag_shift) < kTagValues;
}

// What a drain leaves behind, carried into the next one.
struct State {
    std::uint16_t words = 0;
    std::array<std::uint16_t, kTagValues> per_tag{};
    std::uint16_t unknown_tag = 0;
    std::uint32_t drops = 0;
    std::uint16_t gaps = 0;
    // The counter value the next word should carry, or empty before any word
    // has been seen. This single field is finding two: see the header comment.
    std::optional<std::uint8_t> expect;
};

inline std::uint32_t mask_of(std::uint8_t cnt_bits)
{
    return (cnt_bits >= 8u) ? 0xFFu : ((1u << cnt_bits) - 1u);
}

// The gap between an expected and an observed counter value, IN THE COUNTER'S
// OWN MODULUS. This is the corrected form of step 7's subtraction, which wraps
// at 256 whatever the field's width is: a two-bit counter expecting 3 and
// seeing 0 has advanced by one, and the chapter's line adds 253.
inline std::uint8_t cnt_gap(std::uint8_t expect, std::uint8_t cnt,
                            std::uint8_t cnt_bits)
{
    if (cnt_bits == 0u || cnt_bits > 8u) {
        return 0u;
    }
    const std::uint32_t gap =
        (static_cast<std::uint32_t>(cnt) - static_cast<std::uint32_t>(expect))
        & mask_of(cnt_bits);
    return static_cast<std::uint8_t>(gap);
}

// Decode one drained block into `state`.
//
// `words` is the buffer level, and a value above kMaxWords is refused rather
// than clamped, because silently shortening a read is how a level register
// drives a decode past the end of its landing buffer.
inline std::optional<Error> decode(const Format &fmt,
                                   const std::vector<std::uint8_t> &buf,
                                   std::uint16_t words, State &state)
{
    if (!format_ok(fmt)) {
        return Error::kFormat;
    }
    if (words > kMaxWords) {
        return Error::kWords;
    }
    const std::size_t stride = static_cast<std::size_t>(fmt.word_bytes);
    const std::size_t needed = static_cast<std::size_t>(words) * stride;
    if (buf.size() < needed) {
        // The condition the C cannot detect; see the header comment.
        return Error::kArgument;
    }

    const std::uint32_t cnt_mask = mask_of(fmt.cnt_bits);
    for (std::size_t i = 0u; i < static_cast<std::size_t>(words); ++i) {
        const std::uint32_t first =
            static_cast<std::uint32_t>(buf[i * stride]);
        const std::size_t tag =
            static_cast<std::size_t>(first >> fmt.tag_shift);
        const std::uint8_t cnt =
            static_cast<std::uint8_t>((first >> fmt.cnt_shift) & cnt_mask);

        if (((fmt.known_tags >> tag) & 1u) != 0u) {
            state.per_tag[tag]++;
        } else {
            state.unknown_tag++;
        }

        // The first word of the first drain establishes the sequence instead of
        // being measured against a zero nobody wrote. In the C this is a flag
        // beside the expectation; here it is the absence of the expectation.
        if (state.expect.has_value()) {
            const std::uint8_t gap = cnt_gap(*state.expect, cnt, fmt.cnt_bits);
            if (gap != 0u) {
                state.drops += static_cast<std::uint32_t>(gap);
                state.gaps++;
            }
        }
        state.expect = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(cnt) + 1u) & cnt_mask);
        state.words++;
    }
    return std::nullopt;
}

// Everything step 8's ratio needs, with every assumption a field.
struct Spec {
    std::uint16_t watermark_words = 0;
    std::uint8_t sources = 0;
    std::uint8_t word_bytes = 0;
    std::uint8_t cnt_bits = 0;
    std::array<double, kMaxSources> batch_hz{};
    double bus_hz = 0.0;
    double decode_us_per_word = 0.0;
};

struct Budget {
    double entries_per_s = 0.0;
    double fill_ms = 0.0;
    double chapter_fill_ms = 0.0;
    double drain_us = 0.0;
    double ratio = 0.0;
    // 1 << cnt_bits, finding three: that many consecutive lost slots read as no
    // loss at all, which is a bound on what the drop count can see.
    std::uint32_t undetectable_gap = 0;
    bool ratio_ok = false;
};

// The C's !(x > 0.0), which refuses zero, every negative value and any value
// that is not a number.
inline bool not_above_zero(double x)
{
    return !(x > 0.0);
}

inline bool below_zero(double x)
{
    return !(x >= 0.0);
}

// The fill-to-drain ratio, with the chapter's own form reported beside it.
inline std::optional<Error> budget(const Spec &spec, Budget &out)
{
    if (spec.sources == 0u
        || static_cast<std::size_t>(spec.sources) > kMaxSources) {
        return Error::kSources;
    }
    if (spec.word_bytes == 0u || spec.word_bytes > kMaxWordBytes) {
        return Error::kFormat;
    }
    if (spec.cnt_bits == 0u || spec.cnt_bits > 8u) {
        return Error::kFormat;
    }
    if (spec.watermark_words == 0u || spec.watermark_words > kMaxWords) {
        return Error::kWords;
    }
    if (not_above_zero(spec.bus_hz) || below_zero(spec.decode_us_per_word)) {
        return Error::kRate;
    }
    const std::size_t sources = static_cast<std::size_t>(spec.sources);
    for (std::size_t s = 0u; s < sources; ++s) {
        if (not_above_zero(spec.batch_hz[s])) {
            return Error::kRate;
        }
    }

    // Every batched source writes its own entries into one buffer, so the
    // buffer fills at the SUM of the rates. The chapter's printed form divides
    // by one of them, and both are reported. Accumulated in the array's order,
    // which is part of the specification.
    double entries = 0.0;
    for (std::size_t s = 0u; s < sources; ++s) {
        entries += spec.batch_hz[s];
    }
    out.entries_per_s = entries;
    const double watermark = static_cast<double>(spec.watermark_words);
    out.fill_ms = 1000.0 * watermark / entries;
    out.chapter_fill_ms = 1000.0 * watermark / spec.batch_hz[0];

    // The transaction, as a stated model rather than a measurement: every byte
    // on this bus costs nine bits with its acknowledgement, the register
    // address and the device address are two more bytes, and a start and a stop
    // bracket the whole thing. The decode cost is measured on the board with
    // the cycle counter and arrives here as a parameter.
    const double bytes = watermark * static_cast<double>(spec.word_bytes);
    const double bits = (bytes + 2.0) * 9.0 + 2.0;
    out.drain_us = 1.0e6 * bits / spec.bus_hz
                 + watermark * spec.decode_us_per_word;

    out.ratio = (out.fill_ms * 1000.0) / out.drain_us;
    out.ratio_ok = (out.ratio >= 10.0);
    out.undetectable_gap = (spec.cnt_bits >= 8u)
                         ? 256u : (1u << spec.cnt_bits);
    return std::nullopt;
}

}  // namespace p13

#endif  // P13_FIFODECODE_HPP
