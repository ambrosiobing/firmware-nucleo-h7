// codec/payload.hpp: the C++ variant. C++17, exceptions and RTTI disabled.
//
// The reason to build this is a real one rather than a language preference: the
// field widths become template parameters, so the offsets stop being written by
// hand and the total width is checked by the compiler instead of by a test at
// run time. Two static assertions replace two runtime checks and one class of
// review comment.
//
// What it costs is worth stating plainly rather than hiding. Exceptions cost
// roughly 10 to 30 kB of unwinder tables, which on 2 MB of flash is a choice
// and not a necessity; this build disables them because nothing here throws.
// The two object sizes are published in the README side by side, with no claim
// about which is better until the numbers are there.
//
// The layout must match codec/fields.py and docs/bitorder.md. Nothing generates
// this file, so the static_assert on the total is the tripwire that catches a
// specification change that was not applied here.
#ifndef PAYLOAD_HPP
#define PAYLOAD_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace payload {

template <std::size_t Width, bool Signed>
struct Field {
    static constexpr std::size_t width = Width;
    static constexpr bool is_signed = Signed;
};

using Version  = Field<3,  false>;
using Flags    = Field<4,  false>;
using Sequence = Field<9,  false>;
using Feature  = Field<18, true>;
using Battery  = Field<6,  false>;

constexpr std::size_t total_bits =
    Version::width + Flags::width + Sequence::width
  + Feature::width + Battery::width;

static_assert(total_bits % 8 == 0, "payload must be a whole number of bytes");
static_assert(total_bits == 40, "layout changed: regenerate the vectors by hand");

constexpr std::size_t total_bytes = total_bits / 8;

struct Values {
    std::uint32_t version;
    std::uint32_t flags;
    std::uint32_t sequence;
    std::int32_t  feature;
    std::uint32_t battery;
};

using Buffer = std::array<std::uint8_t, total_bytes>;

namespace detail {

// Offsets computed at compile time from the widths, in declaration order.
// This is the part the C version writes by hand.
constexpr std::size_t off_version  = 0;
constexpr std::size_t off_flags    = off_version  + Version::width;
constexpr std::size_t off_sequence = off_flags    + Flags::width;
constexpr std::size_t off_feature  = off_sequence + Sequence::width;
constexpr std::size_t off_battery  = off_feature  + Feature::width;

static_assert(off_battery + Battery::width == total_bits, "offsets do not close");

template <std::size_t Width>
constexpr std::uint32_t mask()
{
    static_assert(Width > 0 && Width < 32, "width must fit in a uint32_t");
    return static_cast<std::uint32_t>((std::uint64_t{1} << Width) - 1u);
}

template <std::size_t Width>
constexpr void put(Buffer &buf, std::size_t offset, std::uint32_t value)
{
    const std::uint32_t v = value & mask<Width>();
    for (std::size_t i = 0; i < Width; ++i) {
        const std::size_t src = Width - 1u - i;         // MSB of the field first
        const std::size_t dst = offset + i;
        if ((v >> src) & 1u) {
            buf[dst >> 3] = static_cast<std::uint8_t>(
                buf[dst >> 3] | static_cast<std::uint8_t>(0x80u >> (dst & 7u)));
        }
    }
}

template <std::size_t Width>
constexpr std::uint32_t get(const Buffer &buf, std::size_t offset)
{
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < Width; ++i) {
        const std::size_t b = offset + i;
        v = static_cast<std::uint32_t>(
            (v << 1) | ((buf[b >> 3] >> (7u - (b & 7u))) & 1u));
    }
    return v;
}

// Sign extension in signed arithmetic on two values that both fit, for the same
// reason the C version does: converting an out-of-range unsigned to a signed
// type is implementation defined before C++20, and an arithmetic shift of a
// negative value was implementation defined before C++20 too.
template <std::size_t Width>
constexpr std::int32_t sign_extend(std::uint32_t v)
{
    constexpr std::uint32_t sign_bit = std::uint32_t{1} << (Width - 1u);
    if (v & sign_bit) {
        return static_cast<std::int32_t>(v)
             - static_cast<std::int32_t>(sign_bit << 1);
    }
    return static_cast<std::int32_t>(v);
}

}  // namespace detail

constexpr Buffer encode(const Values &in)
{
    Buffer buf{};
    detail::put<Version::width>(buf, detail::off_version, in.version);
    detail::put<Flags::width>(buf, detail::off_flags, in.flags);
    detail::put<Sequence::width>(buf, detail::off_sequence, in.sequence);
    detail::put<Feature::width>(buf, detail::off_feature,
                                static_cast<std::uint32_t>(in.feature));
    detail::put<Battery::width>(buf, detail::off_battery, in.battery);
    return buf;
}

constexpr Values decode(const Buffer &buf)
{
    Values out{};
    out.version  = detail::get<Version::width>(buf, detail::off_version);
    out.flags    = detail::get<Flags::width>(buf, detail::off_flags);
    out.sequence = detail::get<Sequence::width>(buf, detail::off_sequence);
    out.feature  = detail::sign_extend<Feature::width>(
        detail::get<Feature::width>(buf, detail::off_feature));
    out.battery  = detail::get<Battery::width>(buf, detail::off_battery);
    return out;
}

// Both functions are constexpr, so the layout can be checked at compile time
// against a vector computed by hand. This is the one thing the C version cannot
// do, and it is the strongest single argument for the C++ variant. The value
// below is the third vector in test/vectors.json.
namespace selftest {
constexpr Values third_vector{1, 2, 5, -1, 40};
constexpr Buffer third_bytes = encode(third_vector);
static_assert(third_bytes[0] == 0x24, "vector 4 byte 0");
static_assert(third_bytes[1] == 0x05, "vector 4 byte 1");
static_assert(third_bytes[2] == 0xFF, "vector 4 byte 2");
static_assert(third_bytes[3] == 0xFF, "vector 4 byte 3");
static_assert(third_bytes[4] == 0xE8, "vector 4 byte 4");
static_assert(decode(third_bytes).feature == -1, "sign extension at compile time");
}  // namespace selftest

}  // namespace payload

#endif  // PAYLOAD_HPP
