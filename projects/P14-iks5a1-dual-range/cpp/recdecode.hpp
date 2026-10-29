// projects/P14-iks5a1-dual-range/cpp/recdecode.hpp: chapter 14's record, its
// scale codes, the barometric relation and the drain time, in C++17.
//
// The same arithmetic, in the same order, as `../c/recdecode.c`,
// `../python/recdecode.py` and `../rust/src/lib.rs`. The three findings this
// project is about are written out in `../c/recdecode.h` and are not repeated
// here: the event window selects the scale with the worst altitude resolution,
// the altitude is differential and the reference is not in the record, and
// step 10's figure omits the overhead its own comment names.
//
// WHAT C++ ADDS HERE, AND THE MORE INTERESTING ENTRY IS WHAT IT DOES NOT HAVE
// TO DECIDE. The volume's answers:
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
//   P14  the same two again, AND NO DECISION ABOUT THE STANDARD LIBRARY
//
// THE PART WORTH WRITING DOWN. The Rust crate beside this file had to declare
// itself std rather than no_std, because step 9's relation raises a ratio to a
// fractional power and `f64::powf` lives in std: that language splits its
// library into a core that a bare-metal target can always have and a std that
// it cannot. **C++ has no such split in this volume**, because `<cmath>` is
// simply available: `std::pow` costs nothing to reach here, and the question
// of whether this file could link on the board does not arise, since every C++
// variant in this repository is a host filter driven by the tests.
//
// That is not C++ winning. It is the same question asked where it has no
// answer: the C++ freestanding implementation has its own rules about what is
// available, and nothing here exercises them. The Rust's declaration is a
// checked fact about a board target; this file's silence is the absence of a
// claim. Recorded so the list does not read as though the two languages were
// compared on it.
//
// std::optional<Error> for the six refusals, so a refusal is a different type
// rather than a negative return code, and a std::vector for the record bytes,
// so a buffer shorter than the header is expressible rather than undefined.
// Both are P13's answers and both are listed above as recurrences.
//
// THE BUILD PASSES -ffp-contract=off, because the drain time is a multiply
// followed by an add, which gcc fuses at -O2. The oracle compares the
// floating point figures to a relative 1e-12, so a fused result would probably
// pass, and probably passing is not a reason to let a build differ from its
// twins. python/tests/test_float_builds_disable_contraction.py requires the
// flag, reading this header as well as the source.
#ifndef P14_RECDECODE_HPP
#define P14_RECDECODE_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace p14 {

constexpr std::size_t kHdrBytes = 14;
constexpr std::size_t kFsCodes = 5;
constexpr std::size_t kBaroCodes = 2;

constexpr std::uint8_t kBaroNarrow = 0;
constexpr std::uint8_t kBaroWide = 1;

// The flags of step 7, bit for bit.
constexpr std::uint8_t kFlagShortWindow = 0x01;
constexpr std::uint8_t kFlagExtended = 0x02;
constexpr std::uint8_t kFlagBaroBlanked = 0x04;
constexpr std::uint8_t kFlagKnown = 0x07;

// Step 9's exponent and scale factor, as the chapter prints them: the standard
// atmosphere's, left exactly as written so a disagreement with the chapter is
// visible rather than absorbed.
constexpr double kAltScale = 44330.0;
constexpr double kAltExponent = 0.1903;

// The same six refusals as the C, in the same order and with its names.
enum class Error {
    kArgument = 1,
    kFsCode,
    kBaro,
    kFlags,
    kCounts,
    kPressure,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kFsCode:   return "fs_code";
    case Error::kBaro:     return "baro";
    case Error::kFlags:    return "flags";
    case Error::kCounts:   return "counts";
    case Error::kPressure: return "pressure";
    }
    return "unknown";
}

struct Header {
    std::uint32_t tick_event = 0;
    std::uint16_t n_pre = 0;
    std::uint16_t n_post = 0;
    std::uint16_t n_baro = 0;
    std::uint8_t lo_fs_g = 0;
    std::uint8_t hi_fs_g = 0;
    std::uint8_t baro_range = 0;
    std::uint8_t flags = 0;
};

// The code-to-sensitivity table, supplied by the caller rather than compiled
// in, which is the chapter's own reason for putting codes in the record,
// applied one level further down: this bench holds neither sensor's datasheet,
// so a table written into a header would be an invention.
struct Table {
    std::array<double, kFsCodes> lo_mg_per_lsb{};
    std::array<double, kFsCodes> hi_mg_per_lsb{};
    std::array<double, kBaroCodes> baro_lsb_per_hpa{};
    std::uint8_t lo_valid = 0;
    std::uint8_t hi_valid = 0;
    std::uint8_t baro_valid = 0;
};

struct Scales {
    double lo_mg_per_lsb = 0.0;
    double hi_mg_per_lsb = 0.0;
    double baro_lsb_per_hpa = 0.0;
};

// The C's !(x > 0.0), which refuses zero, every negative value and any value
// that is not a number.
inline bool not_above_zero(double x)
{
    return !(x > 0.0);
}

inline std::uint16_t le16(const std::uint8_t *p)
{
    const std::uint32_t low = p[0];
    const std::uint32_t high = p[1];
    return static_cast<std::uint16_t>(low | (high << 8));
}

inline std::uint32_t le32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

// Parse the header from its bytes, little endian, in the field order of the
// struct. Refuses rather than defaults on every code it does not recognise.
//
// The argument is a sized container, so a buffer shorter than the header is a
// refusal here where the C's pointer and length pair leaves it to the caller.
inline std::optional<Error> parse_header(const std::vector<std::uint8_t> &buf,
                                         Header &out)
{
    if (buf.size() < kHdrBytes) {
        return Error::kArgument;
    }
    // Parsed byte by byte and not cast, for the reason the C gives: casting a
    // byte buffer to a packed structure assumes the host's byte order and its
    // tolerance for unaligned members, and the whole value of a second
    // implementation is that it shares no assumption with the first.
    out.tick_event = le32(&buf[0]);
    out.n_pre = le16(&buf[4]);
    out.n_post = le16(&buf[6]);
    out.n_baro = le16(&buf[8]);
    out.lo_fs_g = buf[10];
    out.hi_fs_g = buf[11];
    out.baro_range = buf[12];
    out.flags = buf[13];

    if (static_cast<std::size_t>(out.lo_fs_g) >= kFsCodes
        || static_cast<std::size_t>(out.hi_fs_g) >= kFsCodes) {
        return Error::kFsCode;
    }
    if (static_cast<std::size_t>(out.baro_range) >= kBaroCodes) {
        return Error::kBaro;
    }
    if ((out.flags & static_cast<std::uint8_t>(~kFlagKnown)) != 0u) {
        return Error::kFlags;
    }
    // A window cannot be both shorter than asked for and extended beyond it.
    // The chapter defines the two bits separately and never says they are
    // exclusive, which is the kind of thing a second implementation is for.
    if ((out.flags & kFlagShortWindow) != 0u
        && (out.flags & kFlagExtended) != 0u) {
        return Error::kFlags;
    }
    if (out.n_pre == 0u && out.n_post == 0u) {
        return Error::kCounts;
    }
    if (out.n_baro == 0u && (out.flags & kFlagBaroBlanked) == 0u) {
        return Error::kCounts;
    }
    return std::nullopt;
}

// The scales the record asks for, looked up from its own codes. A code whose
// valid bit is clear is refused rather than read out of the slot: the slot
// holds zero, which would convert every count to zero milli-g and look like a
// quiet channel.
inline std::optional<Error> scales(const Header &hdr, const Table &table,
                                   Scales &out)
{
    const std::size_t lo = hdr.lo_fs_g;
    const std::size_t hi = hdr.hi_fs_g;
    const std::size_t baro = hdr.baro_range;
    if (lo >= kFsCodes || hi >= kFsCodes) {
        return Error::kFsCode;
    }
    if (baro >= kBaroCodes) {
        return Error::kBaro;
    }
    if (((table.lo_valid >> lo) & 1u) == 0u
        || ((table.hi_valid >> hi) & 1u) == 0u) {
        return Error::kFsCode;
    }
    if (((table.baro_valid >> baro) & 1u) == 0u) {
        return Error::kBaro;
    }

    out.lo_mg_per_lsb = table.lo_mg_per_lsb[lo];
    out.hi_mg_per_lsb = table.hi_mg_per_lsb[hi];
    out.baro_lsb_per_hpa = table.baro_lsb_per_hpa[baro];
    if (not_above_zero(out.lo_mg_per_lsb)
        || not_above_zero(out.hi_mg_per_lsb)) {
        return Error::kFsCode;
    }
    if (not_above_zero(out.baro_lsb_per_hpa)) {
        return Error::kBaro;
    }
    return std::nullopt;
}

// Counts to milli-g through the scale the record names. fine selects the fine
// channel, because both are running the whole time.
inline std::optional<Error> counts_to_mg(const Header &hdr, const Table &table,
                                         std::int16_t counts, bool fine,
                                         double &out)
{
    Scales s;
    if (const auto bad = scales(hdr, table, s)) {
        return bad;
    }
    out = static_cast<double>(counts)
        * (fine ? s.lo_mg_per_lsb : s.hi_mg_per_lsb);
    return std::nullopt;
}

// Counts to hectopascals through the range the record names.
inline std::optional<Error> counts_to_hpa(const Header &hdr,
                                          const Table &table,
                                          std::int32_t counts, double &out)
{
    Scales s;
    if (const auto bad = scales(hdr, table, s)) {
        return bad;
    }
    out = static_cast<double>(counts) / s.baro_lsb_per_hpa;
    return std::nullopt;
}

// Step 9's relation exactly, with the reference as an argument BECAUSE THE
// RECORD DOES NOT CARRY ONE, which is finding two. A default here would be the
// compiled constant this project is about.
inline std::optional<Error> altitude_m(double p_hpa, double p0_hpa,
                                       double &out)
{
    if (not_above_zero(p_hpa) || not_above_zero(p0_hpa)) {
        return Error::kPressure;
    }
    out = kAltScale * (1.0 - std::pow(p_hpa / p0_hpa, kAltExponent));
    return std::nullopt;
}

// Metres per least significant bit of pressure at a stated pressure, which is
// what finding one is about.
inline std::optional<Error> altitude_resolution_m(double lsb_per_hpa,
                                                  double p_hpa, double p0_hpa,
                                                  double &out)
{
    if (not_above_zero(lsb_per_hpa) || not_above_zero(p_hpa)
        || not_above_zero(p0_hpa)) {
        return Error::kPressure;
    }
    // The derivative of step 9's relation, times one least significant bit.
    // Taken analytically rather than by differencing two altitudes, because a
    // difference of two nearly equal altitudes is where the resolution of this
    // very calculation would show up as noise.
    const double slope = kAltScale * kAltExponent / p0_hpa
                       * std::pow(p_hpa / p0_hpa, kAltExponent - 1.0);
    out = slope / lsb_per_hpa;
    return std::nullopt;
}

// Step 10's drain time. overhead_bytes is the address and register cost the
// chapter's comment names and its sum omits: pass 0 to reproduce the printed
// figure and 2 for the transaction the comment describes.
inline std::optional<Error> drain_ms(std::uint32_t samples,
                                     std::uint32_t bytes_per_sample,
                                     std::uint32_t overhead_bytes,
                                     double bus_hz, double &out)
{
    if (samples == 0u || bytes_per_sample == 0u) {
        return Error::kCounts;
    }
    if (not_above_zero(bus_hz)) {
        return Error::kPressure;
    }
    // Nine bit times a byte, which is the eight bits and the acknowledgement,
    // exactly as the chapter's comment states it.
    const double bytes = static_cast<double>(samples)
                           * static_cast<double>(bytes_per_sample)
                       + static_cast<double>(overhead_bytes);
    out = 1000.0 * (bytes * 9.0) / bus_hz;
    return std::nullopt;
}

}  // namespace p14

#endif  // P14_RECDECODE_HPP
