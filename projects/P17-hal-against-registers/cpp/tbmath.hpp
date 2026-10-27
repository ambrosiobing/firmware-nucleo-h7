// projects/P17-hal-against-registers/cpp/tbmath.hpp: the timebase in C++17.
//
// The same arithmetic, in the same order, as `../c/tbmath.c`, `../python/tbmath.py`
// and `../rust/src/lib.rs`. Elapsed ticks across a wrap and in either direction,
// the wrap period from a width and a frequency, the instrument's own cost
// subtracted, and the first run kept apart from the median of the remainder.
//
// WHAT C++ ADDS HERE, AND THIS TIME IT IS ALMOST NOTHING. The honest answer
// varies by project in this volume and is worth keeping accurate. P18's cascade
// is one function template where the C needs two forty-line copies, which is a
// real gain. P12's gate costs the C sixty lines the other three do not have.
// Here the arithmetic is four small functions over fixed-width integers, which C
// expresses exactly as well. `std::optional<Error>` makes a refusal a different
// type rather than a negative return code, and `std::array` fixes the sample
// buffer without a macro. That is the whole of it.
//
// ONE THING IS BETTER AND IT IS NOT THE LANGUAGE'S DOING. The subtraction uses a
// comparison before subtracting, exactly as the C does, and not a clever
// unsigned trick. The Rust twin's own README records what that expression does
// in four different ways depending on the compiler and the profile; the useful
// response to that is to not write the expression, in any language.
//
// ON THE DIRECTION. Chapter 17's timebase struct carries a width and a frequency
// and no field for the direction, while its own comment says one of its three
// counters counts down. Direction is an enum argument here, as it is in the
// other three, because the alternative is to be wrong by nearly a whole wrap on
// one of the three peripherals the abstraction exists to make interchangeable.
#ifndef P17_TBMATH_HPP
#define P17_TBMATH_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace p17 {

constexpr std::size_t kMaxSamples = 256;
constexpr std::int32_t kMaxWidthBits = 32;

enum class Direction { kUp, kDown };

// The same seven refusals as the C, in the same order, so a code means the same
// thing in all four implementations.
enum class Error {
    kArgument = 1,
    kWidth,
    kDirection,
    kRange,
    kRate,
    kUnderflow,
    kSamples,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument:  return "argument";
    case Error::kWidth:     return "width";
    case Error::kDirection: return "direction";
    case Error::kRange:     return "range";
    case Error::kRate:      return "rate";
    case Error::kUnderflow: return "underflow";
    case Error::kSamples:   return "samples";
    }
    return "unknown";
}

struct Report {
    std::uint32_t first;
    std::uint32_t median;
    std::uint32_t smallest;
    std::uint32_t largest;
    std::uint32_t count;
};

inline std::uint32_t mask_of(std::uint32_t width_bits)
{
    if (width_bits >= 32u) {
        return 0xFFFFFFFFu;
    }
    return static_cast<std::uint32_t>((1u << width_bits) - 1u);
}

inline std::optional<Error> elapsed(std::uint32_t start, std::uint32_t end,
                                    std::uint32_t width_bits, Direction dir,
                                    std::uint32_t &out)
{
    if (width_bits == 0u || width_bits > 32u) {
        return Error::kWidth;
    }
    const std::uint32_t mask = mask_of(width_bits);
    if ((start & mask) != start || (end & mask) != end) {
        return Error::kRange;
    }
    out = (dir == Direction::kDown) ? ((start - end) & mask)
                                    : ((end - start) & mask);
    return std::nullopt;
}

inline std::optional<Error> wrap_ns(double hz, std::uint32_t width_bits,
                                    std::uint64_t &out)
{
    if (width_bits == 0u || width_bits > 32u) {
        return Error::kWidth;
    }
    // Not finite as well as at or below zero, which the other three now also
    // refuse: an infinite tick rate gives a wrap period of zero nanoseconds,
    // and a plausible number from an impossible input is the thing this project
    // refuses everywhere. Found through clippy on the Rust twin.
    if (!std::isfinite(hz) || hz <= 0.0) {
        return Error::kRate;
    }
    const std::uint64_t ticks = static_cast<std::uint64_t>(1) << width_bits;
    const double ns = static_cast<double>(ticks) * 1.0e9 / hz;
    out = static_cast<std::uint64_t>(ns + 0.5);
    return std::nullopt;
}

inline std::optional<Error> subtract_overhead(std::uint32_t raw,
                                              std::uint32_t overhead,
                                              std::uint32_t &out)
{
    // Compared before subtracting, as in the C. The expression the chapter
    // prints, (b - a) - overhead on an unsigned type, behaves four different
    // ways across the languages and profiles in this project, and the useful
    // response to that is to not write it.
    if (raw < overhead) {
        return Error::kUnderflow;
    }
    out = raw - overhead;
    return std::nullopt;
}

inline void sort_small(std::array<std::uint32_t, kMaxSamples> &v,
                       std::size_t count)
{
    // An insertion sort, allocating nothing, which is the choice the C and the
    // Rust make here too.
    for (std::size_t i = 1u; i < count; ++i) {
        const std::uint32_t key = v[i];
        std::size_t j = i;
        while (j > 0u && v[j - 1u] > key) {
            v[j] = v[j - 1u];
            --j;
        }
        v[j] = key;
    }
}

inline std::optional<Error> report(const std::uint32_t *samples,
                                   std::size_t n, Report &out)
{
    if (samples == nullptr) {
        return Error::kArgument;
    }
    if (n > kMaxSamples || n < 2u) {
        return Error::kSamples;
    }

    std::array<std::uint32_t, kMaxSamples> rest{};
    const std::size_t count = n - 1u;
    for (std::size_t i = 0u; i < count; ++i) {
        rest[i] = samples[i + 1u];
    }
    sort_small(rest, count);

    out.first = samples[0];
    out.smallest = rest[0];
    out.largest = rest[count - 1u];
    out.count = static_cast<std::uint32_t>(count);
    if ((count % 2u) == 1u) {
        out.median = rest[count / 2u];
    } else {
        const std::uint64_t sum = static_cast<std::uint64_t>(rest[count / 2u - 1u])
                                + static_cast<std::uint64_t>(rest[count / 2u]);
        out.median = static_cast<std::uint32_t>(sum / 2u);
    }
    return std::nullopt;
}

}  // namespace p17

#endif  // P17_TBMATH_HPP
