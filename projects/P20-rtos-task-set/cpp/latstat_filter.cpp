// P20's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same four verbs as ../rust/src/main.rs, so one parity test
// drives both with the same code. Each verb takes its counts before its data:
//
//     R <cycles_per_us> <npairs> <cycles repeat>*npairs
//     B <over> <max_us> <count> <npairs> <bin count>*npairs
//     D <count_before> <cycles> <cycles_per_us>
//     S <budget> <free>
//
// Samples arrive run-length encoded, as a cycle figure and a repeat, because
// the chapter's run is a million releases and the oracle stores a recipe
// rather than a list.
//
// THE OPTIONALS BECOME TWO FLAT FIELDS HERE, AND THIS IS THE PLACE TO LOSE
// THEM. The library holds each percentile as a std::optional, which is finding
// three expressed as a type: the value cannot be read without the question
// being answered. The oracle compares seven named numbers across four
// languages and an optional has no spelling in that protocol, so the answer
// prints `median` and `median_resolved` as the C's struct carries them. The
// guarantee holds where the arithmetic is and is given up at the wire format,
// which is the honest boundary for it.
//
// Every value is read as a token and converted with strtoul rather than with
// the stream extractor, and a token with anything left in it is rejected. That
// is a correction P18's filter was given on Monday 26 October 2026, for a
// reason that does not arise here and is kept anyway: `in >> v` on a double
// cannot parse "inf" or "nan" in libstdc++. Every value in this project is an
// integer, so that trap is not reachable, and the habit costs nothing.
//
// A request with anything left over is refused, which caught a real defect in
// P18's test on the same day: ignoring a remainder lets a caller send one
// thing and be answered about another.

#include "latstat.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

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

void refused(p20::Error e)
{
    std::printf("REFUSED %s\n", p20::name(e));
}

// The optional becomes a value and a flag, in the C's spelling, so one oracle
// field list serves every language.
void print_judged(const p20::Report &r, const p20::Verdicts &v)
{
    std::printf("median=%lu median_resolved=%u p999=%lu p999_resolved=%u "
                "max=%lu over=%lu count=%lu vmedian=%u vp999=%u vmax=%u\n",
                static_cast<unsigned long>(r.median_us.value_or(0u)),
                static_cast<unsigned>(r.median_us.has_value() ? 1 : 0),
                static_cast<unsigned long>(r.p999_us.value_or(0u)),
                static_cast<unsigned>(r.p999_us.has_value() ? 1 : 0),
                static_cast<unsigned long>(r.max_us),
                static_cast<unsigned long>(r.over),
                static_cast<unsigned long>(r.count),
                static_cast<unsigned>(p20::code(v.median)),
                static_cast<unsigned>(p20::code(v.p999)),
                static_cast<unsigned>(p20::code(v.max)));
}

void answer_for(const p20::Hist &h)
{
    p20::Report r;
    if (const auto bad = p20::report(h, r)) {
        refused(*bad);
        return;
    }
    print_judged(r, p20::verdict(r, p20::Budget{}));
}

int do_run(std::istringstream &in)
{
    std::uint32_t cycles_per_us = 0;
    std::uint32_t pairs = 0;
    if (!read_u32(in, cycles_per_us) || !read_u32(in, pairs)) {
        return -1;
    }
    p20::Hist h;
    for (std::uint32_t i = 0; i < pairs; ++i) {
        std::uint32_t cycles = 0;
        std::uint32_t repeat = 0;
        if (!read_u32(in, cycles) || !read_u32(in, repeat)) {
            return -1;
        }
        for (std::uint32_t j = 0; j < repeat; ++j) {
            if (const auto bad = p20::record(h, cycles, cycles_per_us)) {
                // A refusal while recording is an answer about a well formed
                // request, so it is reported rather than called malformed.
                refused(*bad);
                return 0;
            }
        }
    }
    answer_for(h);
    return 0;
}

int do_built(std::istringstream &in)
{
    std::uint32_t over = 0;
    std::uint32_t max_us = 0;
    std::uint32_t count = 0;
    std::uint32_t pairs = 0;
    if (!read_u32(in, over) || !read_u32(in, max_us) || !read_u32(in, count)
        || !read_u32(in, pairs)) {
        return -1;
    }
    p20::Hist h;
    h.over = over;
    h.max_us = max_us;
    h.count = count;
    for (std::uint32_t i = 0; i < pairs; ++i) {
        std::uint32_t bin = 0;
        std::uint32_t n = 0;
        if (!read_u32(in, bin) || !read_u32(in, n)) {
            return -1;
        }
        if (bin >= p20::kBins) {
            return -1;
        }
        h.bins[bin] = n;
    }
    answer_for(h);
    return 0;
}

int do_one_sample(std::istringstream &in)
{
    std::uint32_t count_before = 0;
    std::uint32_t cycles = 0;
    std::uint32_t cycles_per_us = 0;
    if (!read_u32(in, count_before) || !read_u32(in, cycles)
        || !read_u32(in, cycles_per_us)) {
        return -1;
    }
    p20::Hist h;
    h.count = count_before;
    if (const auto bad = p20::record(h, cycles, cycles_per_us)) {
        refused(*bad);
        return 0;
    }
    std::printf("ok\n");
    return 0;
}

int do_stack(std::istringstream &in)
{
    std::uint32_t budget = 0;
    std::uint32_t free_bytes = 0;
    if (!read_u32(in, budget) || !read_u32(in, free_bytes)) {
        return -1;
    }
    p20::Stack s;
    if (const auto bad = p20::stack(budget, free_bytes, s)) {
        refused(*bad);
        return 0;
    }
    std::printf("used=%lu permil=%lu within=%u\n",
                static_cast<unsigned long>(s.used),
                static_cast<unsigned long>(s.permil),
                static_cast<unsigned>(s.within ? 1 : 0));
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
        if (verb == "R") {
            rc = do_run(in);
        } else if (verb == "B") {
            rc = do_built(in);
        } else if (verb == "D") {
            rc = do_one_sample(in);
        } else if (verb == "S") {
            rc = do_stack(in);
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
