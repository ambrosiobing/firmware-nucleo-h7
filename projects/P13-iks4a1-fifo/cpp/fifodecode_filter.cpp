// P13's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same three verbs as ../rust/src/main.rs, so one parity test
// drives both with the same requests:
//
//     G <expect> <cnt> <cnt_bits>              -> gap=N
//     D <word_bytes> <tag_shift> <cnt_shift> <cnt_bits> <known_tags>
//       <words> <n> <first byte>*n
//           -> words=N unknown=N drops=N gaps=N tags=a,b,..  (32 counts)
//     B <watermark> <sources> <word_bytes> <cnt_bits> <bus_hz> <decode_us>
//       <rate>*8
//           -> entries=.. fill=.. chapter=.. drain=.. ratio=.. gap=N ok=0|1
//
// THE WORD COUNT AND THE NUMBER OF SUPPLIED BYTES ARE SEPARATE TOKENS, and one
// oracle case is the whole reason: "more words than the landing buffer can
// hold" passes a level of 1025 with no bytes at all, because that refusal is
// decided before anything is read. A protocol deriving the level from the bytes
// could not carry that case, and a filter that cannot carry a case cannot test
// it.
//
// THE FILTER PADS THE BLOCK ITSELF, which is what the C's parity test does on
// its side of the same oracle: each word is its tag byte followed by
// word_bytes - 1 zeros. No part of this arithmetic reads the data bytes.
//
// ALL THIRTY-TWO TAG COUNTS ARE PRINTED, zeros included, as one comma separated
// token, so the test asserts the empty slots as well as the counts the oracle
// states.
//
// Values are read as whole tokens and a token with anything left in it is
// rejected, which is P18's lesson of Monday 26 October 2026: `in >> v` on a
// double cannot parse "inf" or "nan" in libstdc++, and a protocol that cannot
// carry a value the implementation would refuse cannot test the refusal.
//
// Floating point values are printed with "%.17g", enough decimal digits that
// every double reads back as the same bits. The Rust prints the shortest such
// form instead, so the two filters print different text for one value and the
// test compares numbers rather than strings.

#include "fifodecode.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool read_ulong(std::istringstream &in, unsigned long limit,
                unsigned long &out)
{
    std::string token;
    if (!(in >> token)) {
        return false;
    }
    char *end = nullptr;
    const unsigned long v = std::strtoul(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || v > limit) {
        return false;
    }
    out = v;
    return true;
}

bool read_double(std::istringstream &in, double &out)
{
    std::string token;
    if (!(in >> token)) {
        return false;
    }
    char *end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end != token.c_str() && *end == '\0';
}

int do_gap(std::istringstream &in)
{
    unsigned long expect = 0u;
    unsigned long cnt = 0u;
    unsigned long cnt_bits = 0u;
    if (!read_ulong(in, 255u, expect) || !read_ulong(in, 255u, cnt)
        || !read_ulong(in, 255u, cnt_bits)) {
        return -1;
    }
    const std::uint8_t gap = p13::cnt_gap(static_cast<std::uint8_t>(expect),
                                          static_cast<std::uint8_t>(cnt),
                                          static_cast<std::uint8_t>(cnt_bits));
    std::printf("gap=%u\n", static_cast<unsigned>(gap));
    return 0;
}

int do_decode(std::istringstream &in)
{
    unsigned long word_bytes = 0u;
    unsigned long tag_shift = 0u;
    unsigned long cnt_shift = 0u;
    unsigned long cnt_bits = 0u;
    unsigned long known_tags = 0u;
    unsigned long words = 0u;
    unsigned long supplied = 0u;
    if (!read_ulong(in, 255u, word_bytes) || !read_ulong(in, 255u, tag_shift)
        || !read_ulong(in, 255u, cnt_shift) || !read_ulong(in, 255u, cnt_bits)
        || !read_ulong(in, 0xFFFFFFFFu, known_tags)
        || !read_ulong(in, 65535u, words)
        || !read_ulong(in, 65535u, supplied)) {
        return -1;
    }

    p13::Format fmt;
    fmt.word_bytes = static_cast<std::uint8_t>(word_bytes);
    fmt.tag_shift = static_cast<std::uint8_t>(tag_shift);
    fmt.cnt_shift = static_cast<std::uint8_t>(cnt_shift);
    fmt.cnt_bits = static_cast<std::uint8_t>(cnt_bits);
    fmt.known_tags = static_cast<std::uint32_t>(known_tags);

    // One byte per word in the request, padded here to the word length.
    const std::size_t stride =
        static_cast<std::size_t>(word_bytes == 0u ? 1u : word_bytes);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(supplied) * stride,
                                  0u);
    for (unsigned long i = 0u; i < supplied; ++i) {
        unsigned long byte = 0u;
        if (!read_ulong(in, 255u, byte)) {
            return -1;
        }
        buf[static_cast<std::size_t>(i) * stride] =
            static_cast<std::uint8_t>(byte);
    }

    p13::State state;
    if (const auto bad = p13::decode(fmt, buf,
                                     static_cast<std::uint16_t>(words),
                                     state)) {
        std::printf("REFUSED %s\n", p13::name(*bad));
        return 0;
    }
    std::printf("words=%u unknown=%u drops=%lu gaps=%u tags=",
                static_cast<unsigned>(state.words),
                static_cast<unsigned>(state.unknown_tag),
                static_cast<unsigned long>(state.drops),
                static_cast<unsigned>(state.gaps));
    for (std::size_t t = 0u; t < p13::kTagValues; ++t) {
        std::printf("%s%u", t > 0u ? "," : "",
                    static_cast<unsigned>(state.per_tag[t]));
    }
    std::printf("\n");
    return 0;
}

int do_budget(std::istringstream &in)
{
    unsigned long watermark = 0u;
    unsigned long sources = 0u;
    unsigned long word_bytes = 0u;
    unsigned long cnt_bits = 0u;
    if (!read_ulong(in, 65535u, watermark) || !read_ulong(in, 255u, sources)
        || !read_ulong(in, 255u, word_bytes)
        || !read_ulong(in, 255u, cnt_bits)) {
        return -1;
    }
    p13::Spec spec;
    spec.watermark_words = static_cast<std::uint16_t>(watermark);
    spec.sources = static_cast<std::uint8_t>(sources);
    spec.word_bytes = static_cast<std::uint8_t>(word_bytes);
    spec.cnt_bits = static_cast<std::uint8_t>(cnt_bits);
    if (!read_double(in, spec.bus_hz)
        || !read_double(in, spec.decode_us_per_word)) {
        return -1;
    }
    for (std::size_t s = 0u; s < p13::kMaxSources; ++s) {
        if (!read_double(in, spec.batch_hz[s])) {
            return -1;
        }
    }

    p13::Budget out;
    if (const auto bad = p13::budget(spec, out)) {
        std::printf("REFUSED %s\n", p13::name(*bad));
        return 0;
    }
    std::printf("entries=%.17g fill=%.17g chapter=%.17g drain=%.17g "
                "ratio=%.17g gap=%lu ok=%u\n",
                out.entries_per_s, out.fill_ms, out.chapter_fill_ms,
                out.drain_us, out.ratio,
                static_cast<unsigned long>(out.undetectable_gap),
                out.ratio_ok ? 1u : 0u);
    return 0;
}

}  // namespace

int main()
{
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream in(line);
        std::string verb;
        if (!(in >> verb)) {
            std::printf("BAD_REQUEST\n");
            return 1;
        }
        int rc = 0;
        if (verb == "G") {
            rc = do_gap(in);
        } else if (verb == "D") {
            rc = do_decode(in);
        } else if (verb == "B") {
            rc = do_budget(in);
        } else {
            std::printf("BAD_VERB %s\n", verb.c_str());
            return 1;
        }
        if (rc != 0) {
            std::printf("BAD_REQUEST\n");
            return 1;
        }
        std::string leftover;
        if (in >> leftover) {
            std::printf("BAD_REQUEST trailing %s\n", leftover.c_str());
            return 1;
        }
    }
    return 0;
}
