// P17's C++ filter: one request per line on stdin, one answer per line on stdout.
//
// It speaks the same four verbs as ../rust/src/main.rs, so one parity test drives
// both with the same code. Each verb takes its counts before its data:
//
//     E <start> <end> <width> <down>   elapsed ticks, 0 up and 1 down
//     W <hz> <width>                   the wrap period in nanoseconds
//     S <raw> <overhead>               the interval less the instrument's cost
//     R <n> <n samples>                the first run and the median of the rest
//
// Every value is read as a token and converted with strtod or strtoul rather than
// with the stream extractor. That is a correction P18's filter was given on
// Monday 26 October 2026: `in >> v` on a double CANNOT PARSE "inf" OR "nan" in
// libstdc++, and this project's oracle carries both as refusal cases for the
// wrap period, so the protocol has to be able to carry a value the
// implementation is going to reject.
//
// A request with anything left over is refused, which caught a real defect in
// P18's test on the same day: ignoring a remainder lets a caller send one thing
// and be answered about another.
//
// It reads whole lines with getline rather than into a fixed buffer, after P08's
// filter split a twenty-one-thousand-character request into several and answered
// twenty requests with twenty five answers.

#include "tbmath.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool read_u32(std::istringstream &in, std::uint32_t &out)
{
    std::string token;
    if (!(in >> token)) {
        return false;
    }
    char *end = nullptr;
    const unsigned long v = std::strtoul(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || v > 0xFFFFFFFFul) {
        return false;
    }
    out = static_cast<std::uint32_t>(v);
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

void refused(p17::Error e)
{
    std::printf("REFUSED %s\n", p17::name(e));
}

int do_elapsed(std::istringstream &in)
{
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t width = 0;
    std::uint32_t down = 0;
    if (!read_u32(in, start) || !read_u32(in, end) || !read_u32(in, width)
        || !read_u32(in, down)) {
        return -1;
    }
    if (down > 1u) {
        // The C refuses a direction that is neither, and the parity test drives
        // that case, so it is an answer about a well formed request.
        refused(p17::Error::kDirection);
        return 0;
    }
    const p17::Direction dir = (down == 1u) ? p17::Direction::kDown
                                            : p17::Direction::kUp;
    std::uint32_t ticks = 0;
    if (const auto bad = p17::elapsed(start, end, width, dir, ticks)) {
        refused(*bad);
        return 0;
    }
    std::printf("ticks=%lu\n", static_cast<unsigned long>(ticks));
    return 0;
}

int do_wrap(std::istringstream &in)
{
    double hz = 0.0;
    std::uint32_t width = 0;
    if (!read_double(in, hz) || !read_u32(in, width)) {
        return -1;
    }
    std::uint64_t ns = 0;
    if (const auto bad = p17::wrap_ns(hz, width, ns)) {
        refused(*bad);
        return 0;
    }
    std::printf("ns=%llu\n", static_cast<unsigned long long>(ns));
    return 0;
}

int do_subtract(std::istringstream &in)
{
    std::uint32_t raw = 0;
    std::uint32_t overhead = 0;
    if (!read_u32(in, raw) || !read_u32(in, overhead)) {
        return -1;
    }
    std::uint32_t ticks = 0;
    if (const auto bad = p17::subtract_overhead(raw, overhead, ticks)) {
        refused(*bad);
        return 0;
    }
    std::printf("ticks=%lu\n", static_cast<unsigned long>(ticks));
    return 0;
}

int do_report(std::istringstream &in)
{
    std::uint32_t n = 0;
    if (!read_u32(in, n)) {
        return -1;
    }
    std::vector<std::uint32_t> samples;
    samples.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint32_t v = 0;
        if (!read_u32(in, v)) {
            return -1;
        }
        samples.push_back(v);
    }
    p17::Report r{};
    const std::uint32_t *data = samples.empty() ? nullptr : samples.data();
    // An empty request is a report of nothing, which is a refusal and not a
    // null pointer fault, so the pointer is only null when there is no data to
    // point at and the refusal for that is the sample count.
    if (data == nullptr) {
        refused(p17::Error::kSamples);
        return 0;
    }
    if (const auto bad = p17::report(data, samples.size(), r)) {
        refused(*bad);
        return 0;
    }
    std::printf("first=%lu median=%lu smallest=%lu largest=%lu count=%lu\n",
                static_cast<unsigned long>(r.first),
                static_cast<unsigned long>(r.median),
                static_cast<unsigned long>(r.smallest),
                static_cast<unsigned long>(r.largest),
                static_cast<unsigned long>(r.count));
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
        if (verb == "E") {
            rc = do_elapsed(in);
        } else if (verb == "W") {
            rc = do_wrap(in);
        } else if (verb == "S") {
            rc = do_subtract(in);
        } else if (verb == "R") {
            rc = do_report(in);
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
