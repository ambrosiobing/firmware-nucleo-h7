// projects/P19-caches-mpu-dma/cpp/mpualign.hpp: chapter 19's criteria 4 and 5,
// in C++17.
//
// The same arithmetic, in the same order, as `../c/mpualign.c`,
// `../python/mpualign.py` and `../rust/src/lib.rs`. The four findings this
// project is about are written out in `../c/mpualign.h` and are not repeated
// here.
//
// THIS IS THE LAST OF EIGHTY IMPLEMENTATIONS, four languages across twenty
// projects, and the question it answers is the one asked of every C++ variant:
// what does the language add. The answer here is **almost nothing, and the
// reason is nameable**, which makes it the most useful entry in the list rather
// than the emptiest.
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
//   P15  an enum class and -Wswitch remove the default arm, which is most of
//        the Rust's answer and not all of it
//   P19  ALMOST NOTHING, AND THE RUST HAD SOMETHING ON THE SAME FINDING
//
// WHERE THE RUST HAD SOMETHING AND THIS HAS NOT. Finding four is that the
// aligned end can need the value 2 to the 32nd, which a uint32_t cannot hold,
// and that storing zero there is the failure rather than an approximation of
// it. The Rust asks the 32 bit type:
//
//     let last = addr.checked_add(len).ok_or(Error::Wrap)?;
//     let end = last.checked_next_multiple_of(line).ok_or(Error::Wrap)?;
//
// C++17 has no checked arithmetic and no next_multiple_of. `std::popcount` and
// `std::bit_width` arrived in C++20 with `<bit>`, and the compiler builtins
// `__builtin_add_overflow` and `__builtin_mul_overflow` are a gcc extension
// this volume does not rely on. **So this file widens to 64 bits exactly as the
// C does**, and the region logarithm counts shifts exactly as the C does.
//
// That is the honest answer and it is worth the paragraph: the two places where
// the Rust's standard library expressed the question better are the two places
// C++17's does not, so on this project the C++ is the C with a std::optional
// and a namespace. P17 reached the same conclusion from a different direction,
// four small functions over fixed-width integers, and two instances make it a
// property of the problem rather than a remark about one file: **when the
// arithmetic is already in the narrowest type that fits, there is nothing for a
// richer language to carry.**
//
// What it does add is the one thing it has added in nine of the twelve entries
// above: std::optional<Error> makes a refusal a different type rather than a
// negative return code, so the answer cannot be read without the question being
// answered first.
//
// EVERY QUANTITY HERE IS AN UNSIGNED INTEGER, so there is no floating point and
// no contraction flag applies.
#ifndef P19_MPUALIGN_HPP
#define P19_MPUALIGN_HPP

#include <cstdint>
#include <optional>

namespace p19 {

// This part's cache line, measured by P19's placement experiment on Saturday
// 3 October 2026: the cold cost tracked the offset within a 32 byte granule,
// 3220.99 cycles at offset 0 against 3163.98 at offset 16, and was flat at
// 2378.99 across all four offsets once cached. EVERY FUNCTION STILL TAKES THE
// LINE AS AN ARGUMENT, because a figure measured on one part is not a constant
// of the arithmetic.
constexpr std::uint32_t kLine = 32;

// ARMv7-M's smallest region, and the largest this signature can express. The
// architecture also allows a region covering the whole address space, 2 to the
// 32nd, which does not fit in a uint32_t.
constexpr std::uint32_t kMinRegion = 32;
constexpr std::uint32_t kMaxRegion = 0x80000000u;

// The same six refusals as the C, in the same order and with its names.
enum class Error {
    kArgument = 1,
    kLine,
    kLength,
    kWrap,
    kSize,
    kAlign,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kLine:     return "line";
    case Error::kLength:   return "length";
    case Error::kWrap:     return "wrap";
    case Error::kSize:     return "size";
    case Error::kAlign:    return "align";
    }
    return "unknown";
}

// The whole lines a maintenance operation touches, with the two shared lines
// reported separately so a caller can be refused for the operation that cannot
// tolerate them, which is finding one.
struct Range {
    std::uint32_t base = 0;
    std::uint32_t end = 0;
    std::uint32_t bytes = 0;
    std::uint32_t lines = 0;
    bool head_shared = false;
    bool tail_shared = false;
};

inline bool is_power_of_two(std::uint32_t v)
{
    return (v != 0u) && ((v & (v - 1u)) == 0u);
}

// The whole lines a maintenance operation over [addr, addr + len) touches.
inline std::optional<Error> maint_range(std::uint32_t addr, std::uint32_t len,
                                        std::uint32_t line, Range &out)
{
    // A power of two, because the rounding below is a mask and not a division,
    // and because no cache has a line that is not one.
    if (!is_power_of_two(line)) {
        return Error::kLine;
    }
    if (len == 0u) {
        return Error::kLength;
    }

    // Finding four, checked before anything is rounded, and computed in 64 bits
    // for the same reason the C gives: a 32 bit test written as
    // addr + len < addr is correct and says nothing about the ROUNDED end,
    // which can cross the boundary when the unrounded sum did not. The header
    // says why C++17 has nothing better to offer here and the Rust does.
    const std::uint64_t last =
        static_cast<std::uint64_t>(addr) + static_cast<std::uint64_t>(len);
    const std::uint64_t mask = static_cast<std::uint64_t>(line) - 1u;
    const std::uint64_t aligned_end = (last + mask) & ~mask;
    // AT OR ABOVE 2 TO THE 32ND, NOT ABOVE IT, and the boundary is the point:
    // end is one past the last address, so a range ending exactly at the top of
    // the address space has no value a uint32_t can hold, and storing zero IS
    // the failure this finding is about rather than a near miss.
    if (aligned_end >= 0x100000000ull) {
        return Error::kWrap;
    }

    const std::uint32_t base = addr & static_cast<std::uint32_t>(~mask);
    out.base = base;
    out.end = static_cast<std::uint32_t>(aligned_end);
    out.bytes = out.end - base;
    out.lines = out.bytes / line;
    out.head_shared = (addr & static_cast<std::uint32_t>(mask)) != 0u;
    out.tail_shared = (last & mask) != 0u;
    return std::nullopt;
}

// Criterion 4, as one answer: aligned to a line AND a whole number of lines. A
// shared line in either position is a failure of alignment, which is the
// criterion's own word for it, and finding two is that the two halves fix
// different ends.
inline std::optional<Error> buffer_check(std::uint32_t addr,
                                         std::uint32_t len,
                                         std::uint32_t line)
{
    Range r;
    if (const auto bad = maint_range(addr, len, line, r)) {
        return bad;
    }
    if (r.head_shared || r.tail_shared) {
        return Error::kAlign;
    }
    return std::nullopt;
}

// Criterion 5: an ARMv7-M region's SIZE field, which is log2(size) - 1, with
// the base checked against the region's OWN size.
inline std::optional<Error> region_encode(std::uint32_t base,
                                          std::uint32_t size,
                                          std::uint8_t &size_field)
{
    // No upper test, and the absence is deliberate: every power of two a
    // uint32_t can hold is at or below 2 to the 31st, which is kMaxRegion, so a
    // size above it cannot arrive here AND be a power of two. A size >
    // kMaxRegion test would be a guard that can never fire, and a check that
    // cannot fail is worse than no check because it reads as coverage.
    if (!is_power_of_two(size) || size < kMinRegion) {
        return Error::kSize;
    }
    // Finding three: the base must be a multiple of the region's own size and
    // not of the cache line, so a buffer that satisfies criterion 4 can still
    // fail here.
    if ((base & (size - 1u)) != 0u) {
        return Error::kAlign;
    }

    // Counted rather than taken from a table, exactly as the C does.
    // std::bit_width is C++20 and the Rust's ilog2 has no C++17 counterpart.
    std::uint32_t log2 = 0u;
    std::uint32_t v = size;
    while (v > 1u) {
        v >>= 1;
        log2 += 1u;
    }
    size_field = static_cast<std::uint8_t>(log2 - 1u);
    return std::nullopt;
}

}  // namespace p19

#endif  // P19_MPUALIGN_HPP
