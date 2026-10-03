// projects/P05-framing-crc/cpp/frame_filter.cpp: P05's C++ framing as a filter.
//
// One request per line on stdin, one answer per line on stdout. Three verbs:
//
//   C <hex>      the CRC-16 of those bytes, as four uppercase hex digits
//   E <hex>      the frame for that payload, delimiter included, or REFUSED
//   D <hex>      what lay between two delimiters, decoded: a verdict and the
//                payload as hex, or - when there is none
//
// A blank hex field is a zero-length input, so "E " frames the empty payload
// and "D " is an empty run between two delimiters.
//
// The Rust filter speaks the same three verbs, and python/tests/test_frame_parity.py
// drives both with the same code, so neither parses the random cases or the
// corruption set: those stay in the test, which owns them. Same shape as P09's
// cpp_filter.cpp: stdio, fixed buffers, no heap, one process per run.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "frame.hpp"

namespace {

// Hex to bytes. Returns the byte count, or -1 on an odd length or a bad digit,
// so a malformed request is reported rather than half decoded.
int from_hex(const char* text, std::uint8_t* out, std::size_t cap)
{
    const std::size_t n = std::strlen(text);
    if (n % 2 != 0 || n / 2 > cap) {
        return -1;
    }
    for (std::size_t i = 0; i < n; i += 2) {
        unsigned value = 0;
        for (std::size_t k = 0; k < 2; ++k) {
            const char c = text[i + k];
            unsigned nibble = 0;
            if (c >= '0' && c <= '9') {
                nibble = static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<unsigned>(c - 'a') + 10u;
            } else if (c >= 'A' && c <= 'F') {
                nibble = static_cast<unsigned>(c - 'A') + 10u;
            } else {
                return -1;
            }
            value = (value << 4) | nibble;
        }
        out[i / 2] = static_cast<std::uint8_t>(value);
    }
    return static_cast<int>(n / 2);
}

void print_hex(const std::uint8_t* data, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i) {
        std::printf("%02X", data[i]);
    }
}

}  // namespace

int main()
{
    char line[1024];
    std::uint8_t in[512];
    std::uint8_t out[512];

    while (std::fgets(line, sizeof line, stdin) != nullptr) {
        // A line that filled the buffer without reaching a newline is refused
        // rather than answered. fgets would otherwise return the rest of it as
        // a second request and this filter would answer both, which is how
        // P08's filter came to return 25 answers for 20 requests on Saturday
        // 3 October 2026. The longest request here is a verb and 64 bytes of
        // payload as hex, so the buffer is ample; refusing costs nothing and
        // removes the silent version of the failure.
        if (std::strchr(line, '\n') == nullptr && std::feof(stdin) == 0) {
            std::fprintf(stderr,
                         "a request line longer than %zu bytes was refused rather "
                         "than split\n",
                         sizeof line);
            return EXIT_FAILURE;
        }
        // Strip the line ending, then split "V hex" at the first space. A line
        // with no verb is skipped, as the P09 filter skips a malformed line.
        line[std::strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        const char verb = line[0];
        const char* hex = (line[1] == ' ') ? line + 2 : line + 1;

        const int n = from_hex(hex, in, sizeof in);
        if (n < 0) {
            std::printf("BAD_HEX\n");
            continue;
        }
        const std::size_t len = static_cast<std::size_t>(n);

        switch (verb) {
        case 'C':
            std::printf("%04X\n", p05::crc16(in, len));
            break;

        case 'E': {
            const std::size_t m = p05::frame_encode(in, len, out, sizeof out);
            if (m == 0) {
                std::printf("REFUSED\n");
            } else {
                print_hex(out, m);
                std::printf("\n");
            }
            break;
        }

        case 'D': {
            std::size_t got = 0;
            const p05::result r = p05::frame_decode(in, len, out, sizeof out, got);
            std::printf("%s ", p05::name(r));
            if (r == p05::result::ok) {
                print_hex(out, got);
            } else {
                std::printf("-");
            }
            std::printf("\n");
            break;
        }

        default:
            std::printf("BAD_VERB\n");
            break;
        }
    }
    return EXIT_SUCCESS;
}
