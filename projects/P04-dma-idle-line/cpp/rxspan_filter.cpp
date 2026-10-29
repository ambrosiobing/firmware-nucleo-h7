// P04's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same one verb as ../rust/src/main.rs, so one parity test drives
// both with the same code:
//
//     N <size> <old_pos> <pos>
//         -> spans=n [off0=.. len0=.. [off1=.. len1=..]] peak=.. near_lap=..
//            calls=.. deliveries=..
//
// Each request runs on FRESH statistics, which is how the oracle states its
// cases: one call, one answer, so the peak and the near-lap count describe that
// call rather than the run.
//
// THE ONE PLACE THAT CONSUMES THE ANSWER USES std::visit, AND THAT IS THE
// POINT OF THE TYPE. The library returns a std::variant, which is the same sum
// type the Rust gets from an enum: there is no count beside an array to
// disagree with, and no slot to read that is not there. std::visit over it is
// checked for exhaustiveness at compile time, so an overload set missing an
// alternative does not build. Using std::get_if here instead would have been
// shorter and would have handed the guarantee back, because a sequence of
// get_if checks is exhaustive only by the author's discipline.
//
// Values are read as tokens and converted with strtoul, and a token with
// anything left in it is rejected, which is P18's lesson of Monday 26 October
// 2026 kept even though this project has no floating point for the stream
// extractor to mishandle. A request with anything left over is refused.

#include "rxspan.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool read_u16(std::istringstream &in, std::uint16_t &out)
{
    std::string token;
    if (!(in >> token)) {
        return false;
    }
    char *end = nullptr;
    const unsigned long v = std::strtoul(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || v > 0xFFFFul) {
        return false;
    }
    out = static_cast<std::uint16_t>(v);
    return true;
}

// The spans flattened into the C's spelling, so one oracle field list serves
// every language. Exhaustive by the compiler rather than by inspection: adding
// an alternative to Delivery would stop this building.
std::string spans_text(const p04::Delivery &d)
{
    return std::visit(
        [](const auto &held) -> std::string {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, std::monostate>) {
                return "";
            } else if constexpr (std::is_same_v<Held, p04::Span>) {
                return " off0=" + std::to_string(held.off)
                     + " len0=" + std::to_string(held.len);
            } else {
                return " off0=" + std::to_string(held.first.off)
                     + " len0=" + std::to_string(held.first.len)
                     + " off1=" + std::to_string(held.second.off)
                     + " len1=" + std::to_string(held.second.len);
            }
        },
        d);
}

int do_next(std::istringstream &in)
{
    std::uint16_t size = 0;
    std::uint16_t old_pos = 0;
    std::uint16_t pos = 0;
    if (!read_u16(in, size) || !read_u16(in, old_pos)
        || !read_u16(in, pos)) {
        return -1;
    }

    p04::Stats stats;
    p04::Delivery delivery;
    if (const auto bad = p04::next_spans(size, old_pos, pos, stats,
                                         delivery)) {
        std::printf("REFUSED %s\n", p04::name(*bad));
        return 0;
    }
    std::printf("spans=%lu%s peak=%lu near_lap=%lu calls=%lu deliveries=%lu\n",
                static_cast<unsigned long>(p04::count_of(delivery)),
                spans_text(delivery).c_str(),
                static_cast<unsigned long>(stats.peak_advance),
                static_cast<unsigned long>(stats.near_lap),
                static_cast<unsigned long>(stats.calls),
                static_cast<unsigned long>(stats.deliveries));
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
        if (verb != "N") {
            std::printf("BAD_VERB %s\n", verb.c_str());
            return 1;
        }
        if (do_next(in) != 0) {
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
