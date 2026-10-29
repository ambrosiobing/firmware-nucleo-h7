// P11's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same three verbs as ../rust/src/main.rs, so one parity test
// drives both with the same requests:
//
//     C <n_urcs> <urc field>*n <line field>   -> class=<name>
//     D <now_ms> <deadline_ms>                -> due=1 | due=0
//     B <attempt> <base_ms> <cap_ms> <rand>   -> delay=NNN | REFUSED backoff
//
// A FIELD IS A LENGTH AND THEN HEX, AND THE ORACLE IS THE REASON. Nine of the
// seventeen classify cases contain a space, four contain a comma or a double
// quote, and one is the EMPTY line, which is a class of its own here, so ten of
// the seventeen cannot cross a whitespace separated protocol at all and only
// seven can. A filter that cannot carry a case cannot test it, which this
// volume has already paid for once: an oracle case the C's signature could not
// express cost two red commits, and the answer then was to change the signature
// rather than exempt the case.
//
// The length travels with the hex because a whitespace split cannot produce an
// empty token, and a hex token whose decoded length disagrees with the stated
// one is refused rather than classified as the shorter line.
//
// THE REFUSAL CROSSES AS A WORD AND NOT AS 0xFFFFFFFF, for the same reason
// backoff_ms returns an optional: the C's sentinel is a legal delay of about
// forty-nine days, so carried as a number the refusal would be
// indistinguishable from an answer and a test comparing numbers could not tell
// them apart.
//
// Numbers are read as whole tokens and a token with anything left in it is
// rejected, which is P18's lesson of Monday 26 October 2026.

#include "atparse.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
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

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

// A length, and then the hex of that many bytes when the length is not zero.
bool read_field(std::istringstream &in, std::string &out)
{
    out.clear();
    unsigned long want = 0u;
    if (!read_ulong(in, 100000u, want)) {
        return false;
    }
    if (want == 0u) {
        return true;
    }
    std::string token;
    if (!(in >> token)) {
        return false;
    }
    if (token.size() != want * 2u) {
        return false;
    }
    for (std::size_t i = 0u; i < token.size(); i += 2u) {
        const int hi = hex_digit(token[i]);
        const int lo = hex_digit(token[i + 1u]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

int do_classify(std::istringstream &in)
{
    unsigned long count = 0u;
    if (!read_ulong(in, 1000u, count)) {
        return -1;
    }
    // The prefixes own their bytes and the views point into them, so the
    // vector of strings must outlive the vector of views; both live here.
    std::vector<std::string> owned;
    owned.reserve(count);
    for (unsigned long i = 0u; i < count; ++i) {
        std::string prefix;
        if (!read_field(in, prefix)) {
            return -1;
        }
        owned.push_back(prefix);
    }
    std::string line;
    if (!read_field(in, line)) {
        return -1;
    }

    std::vector<std::string_view> urcs;
    urcs.reserve(owned.size());
    for (const std::string &s : owned) {
        urcs.emplace_back(s);
    }
    std::printf("class=%s\n", p11::name(p11::classify(line, urcs)));
    return 0;
}

int do_due(std::istringstream &in)
{
    unsigned long now = 0u;
    unsigned long deadline = 0u;
    if (!read_ulong(in, 0xFFFFFFFFu, now)
        || !read_ulong(in, 0xFFFFFFFFu, deadline)) {
        return -1;
    }
    const bool answer = p11::due(static_cast<std::uint32_t>(now),
                                 static_cast<std::uint32_t>(deadline));
    std::printf("due=%u\n", answer ? 1u : 0u);
    return 0;
}

int do_backoff(std::istringstream &in)
{
    unsigned long attempt = 0u;
    unsigned long base = 0u;
    unsigned long cap = 0u;
    unsigned long draw = 0u;
    if (!read_ulong(in, 255u, attempt) || !read_ulong(in, 0xFFFFFFFFu, base)
        || !read_ulong(in, 0xFFFFFFFFu, cap)
        || !read_ulong(in, 0xFFFFFFFFu, draw)) {
        return -1;
    }
    const auto answer = p11::backoff_ms(static_cast<std::uint8_t>(attempt),
                                        static_cast<std::uint32_t>(base),
                                        static_cast<std::uint32_t>(cap),
                                        static_cast<std::uint32_t>(draw));
    if (!answer.has_value()) {
        std::printf("REFUSED backoff\n");
        return 0;
    }
    std::printf("delay=%lu\n", static_cast<unsigned long>(*answer));
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
        if (verb == "C") {
            rc = do_classify(in);
        } else if (verb == "D") {
            rc = do_due(in);
        } else if (verb == "B") {
            rc = do_backoff(in);
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
