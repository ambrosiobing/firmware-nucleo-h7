// projects/P05-framing-crc/cpp/frame.hpp: P05's framing in C++17, header only.
//
// The same format as the C in ../c/ and the Python in ../python/twin.py, written
// independently rather than wrapped, because the comparison between the four
// languages only means something if each is idiomatic for its own.
//
// On the wire:  [ COBS( payload || crc16(payload) ) ] [ 0x00 ]
//
// What C++ adds over the C, and it is one thing: the checksum is constexpr, so
// the published check value 0x29B1 over "123456789" is asserted by the compiler
// at the bottom of this file. The C asserts it in a test; here a translation
// unit that includes this header does not compile unless the parameters are the
// published ones. That is the same property P09's C++ has for its fourth golden
// vector, and it is the reason this is a header and not a .cpp.
//
// The subset: no exceptions, no RTTI, no heap, no iostream. std::optional is
// used for one reason stated at cobs_decode, and std::array for buffers whose
// size the format fixes. Builds with -fno-exceptions -fno-rtti.
#ifndef P05_FRAME_HPP
#define P05_FRAME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace p05 {

// The six parameters, and all six matter. The reversal settings are the ones
// that go wrong in practice: a checksum with the input bits reflected is a
// perfectly good checksum that agrees with nothing published.
inline constexpr std::uint16_t crc16_poly = 0x1021;
inline constexpr std::uint16_t crc16_init = 0xFFFF;
inline constexpr std::uint16_t crc16_check_value = 0x29B1;  // over "123456789"

inline constexpr std::uint8_t delimiter = 0x00;
inline constexpr std::size_t crc_bytes = 2;
inline constexpr std::size_t max_payload = 64;

// Bitwise, as the C reference is, and constexpr. A table would be faster and
// would move the check value from the compiler to a test.
constexpr std::uint16_t crc16(const std::uint8_t* data, std::size_t len)
{
    std::uint16_t crc = crc16_init;
    for (std::size_t i = 0; i < len; ++i) {
        // No input reflection: the byte enters the high half as it is.
        crc = static_cast<std::uint16_t>(crc ^ static_cast<std::uint16_t>(data[i] << 8));
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0
                      ? static_cast<std::uint16_t>((crc << 1) ^ crc16_poly)
                      : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;  // no final exclusive-or, no output reflection
}

// ------------------------------------------------------------------- COBS

constexpr std::size_t cobs_encode_max(std::size_t len)
{
    return len + (len / 254) + 1;  // one code byte per run of up to 254, plus one
}

// The encoded length, or 0 when cap is too small. The delimiter is not
// appended: it belongs to the stream, not to the encoding.
inline std::size_t cobs_encode(const std::uint8_t* src, std::size_t len,
                               std::uint8_t* dst, std::size_t cap)
{
    if (src == nullptr || dst == nullptr || cap < cobs_encode_max(len)) {
        return 0;
    }
    std::size_t code_at = 0;  // where the code byte for the run in progress goes
    std::size_t out = 1;      // the slot after it
    std::uint8_t code = 1;    // counts itself

    for (std::size_t i = 0; i < len; ++i) {
        if (src[i] != 0) {
            dst[out++] = src[i];
            ++code;
        }
        // A zero ends the run and is represented by the code rather than
        // written. A run of 254 non-zero bytes ends too: the code cannot count
        // higher.
        if (src[i] == 0 || code == 0xFF) {
            dst[code_at] = code;
            code_at = out++;
            code = 1;
        }
    }
    dst[code_at] = code;
    return out;
}

// The decoded length, or nothing on a corrupt frame.
//
// std::optional rather than the C's 0-means-failure, and this is the one place
// the C++ departs from the C deliberately. A frame consisting of the single byte
// 0x01 decodes to zero bytes, legitimately, and the C cannot tell that apart
// from a failure: both return 0, and frame_decode in C therefore reports that
// frame as a stuffing error when it is a frame that is too short. The Python
// twin tells the two apart with None, and so does this. The parity test pins
// that single divergence by name rather than hiding it.
inline std::optional<std::size_t> cobs_decode(const std::uint8_t* src, std::size_t len,
                                              std::uint8_t* dst, std::size_t cap)
{
    if (src == nullptr || dst == nullptr) {
        return std::nullopt;
    }
    std::size_t in = 0;
    std::size_t out = 0;

    while (in < len) {
        const std::uint8_t code = src[in++];
        if (code == 0) {
            return std::nullopt;  // the delimiter cannot appear inside a frame
        }
        for (std::uint8_t i = 1; i < code; ++i) {
            if (in >= len || out >= cap) {
                return std::nullopt;  // the code promised more than arrived
            }
            dst[out++] = src[in++];
        }
        // A run shorter than 254 ended on a zero in the original, so put it
        // back. A run of exactly 254 did not, and a run at the end did not.
        if (code != 0xFF && in < len) {
            if (out >= cap) {
                return std::nullopt;
            }
            dst[out++] = 0;
        }
    }
    return out;
}

// ------------------------------------------------------------------ frame

// Every way a decode can end, the same seven as the C's frame_result_t and in
// the same order, because the counts are different findings.
enum class result { ok, empty, args, stuffing, too_short, too_long, checksum };

constexpr const char* name(result r)
{
    switch (r) {
    case result::ok:        return "ok";
    case result::empty:     return "empty";
    case result::args:      return "args";
    case result::stuffing:  return "stuffing";
    case result::too_short: return "too_short";
    case result::too_long:  return "too_long";
    case result::checksum:  return "checksum";
    }
    return "?";
}

constexpr std::size_t frame_encode_max(std::size_t payload_len)
{
    return cobs_encode_max(payload_len + crc_bytes) + 1;  // plus the delimiter
}

// The frame length including the delimiter, or 0 on failure.
inline std::size_t frame_encode(const std::uint8_t* payload, std::size_t len,
                                std::uint8_t* out, std::size_t cap)
{
    if (payload == nullptr || out == nullptr || len > max_payload) {
        return 0;
    }
    if (cap < frame_encode_max(len)) {
        return 0;
    }
    // Payload then checksum as one buffer, so the stuffing covers both.
    std::array<std::uint8_t, max_payload + crc_bytes> body{};
    for (std::size_t i = 0; i < len; ++i) {
        body[i] = payload[i];
    }
    const std::uint16_t crc = crc16(payload, len);
    body[len] = static_cast<std::uint8_t>(crc >> 8);  // most significant first
    body[len + 1] = static_cast<std::uint8_t>(crc & 0xFFu);

    const std::size_t stuffed = cobs_encode(body.data(), len + crc_bytes, out, cap - 1);
    if (stuffed == 0) {
        return 0;
    }
    out[stuffed] = delimiter;
    return stuffed + 1;
}

// Decodes what lay between two delimiters. On ok, payload_len is set and the
// payload written; on anything else payload_len is 0 and nothing is written.
inline result frame_decode(const std::uint8_t* stuffed, std::size_t len,
                           std::uint8_t* payload, std::size_t cap, std::size_t& payload_len)
{
    payload_len = 0;
    if (stuffed == nullptr || payload == nullptr) {
        return result::args;
    }
    if (len == 0) {
        return result::empty;  // an idle line, discarded rather than reported
    }
    std::array<std::uint8_t, max_payload + crc_bytes> body{};
    const std::optional<std::size_t> decoded = cobs_decode(stuffed, len, body.data(), body.size());
    if (!decoded) {
        return result::stuffing;
    }
    if (*decoded < crc_bytes + 1) {
        return result::too_short;  // shorter than its own checksum
    }
    const std::size_t n = *decoded - crc_bytes;
    const std::uint16_t got = static_cast<std::uint16_t>((body[n] << 8) | body[n + 1]);
    const std::uint16_t want = crc16(body.data(), n);
    if (got != want) {
        return result::checksum;  // never partially delivered
    }
    if (n > cap) {
        return result::too_long;
    }
    for (std::size_t i = 0; i < n; ++i) {
        payload[i] = body[i];
    }
    payload_len = n;
    return result::ok;
}

// The check value, asserted by the compiler. If the six parameters above are
// not the published ones, nothing that includes this header compiles.
namespace detail {
inline constexpr std::array<std::uint8_t, 9> check_input{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static_assert(crc16(check_input.data(), check_input.size()) == crc16_check_value,
              "the CRC-16 parameters do not reproduce the published check value 0x29B1");
}  // namespace detail

}  // namespace p05

#endif  // P05_FRAME_HPP
