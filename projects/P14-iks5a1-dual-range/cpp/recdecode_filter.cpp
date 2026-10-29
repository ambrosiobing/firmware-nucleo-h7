// P14's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same six verbs as ../rust/src/main.rs, so one parity test
// drives both with the same requests:
//
//     H <n> <byte>*n                     -> tick=.. pre=.. post=.. baro=..
//                                           lo=.. hi=.. range=.. flags=..
//     M <n> <byte>*n <table> <counts> <fine>   -> mg=..
//     P <n> <byte>*n <table> <counts>          -> hpa=..
//     A <p_hpa> <p0_hpa>                       -> m=..
//     R <lsb_per_hpa> <p_hpa> <p0_hpa>         -> m=..
//     D <samples> <bytes_per_sample> <overhead> <bus_hz>  -> ms=..
//
// and any of them can answer REFUSED with one of the six names the C uses.
//
// THE BYTE COUNT TRAVELS WITH THE BYTES, and one oracle case is the whole
// reason: "a buffer shorter than the header" is THIRTEEN bytes, and its point
// is that the parse refuses before reading a field. The Rust's protocol carried
// exactly fourteen until Thursday 29 October 2026 and could not express that
// case; this file was written after that was found, so it carries the count
// from the start. A filter that cannot carry a case cannot test it.
//
// THE TABLE IS FIFTEEN TOKENS AND TRAVELS IN EVERY REQUEST THAT NEEDS IT, and
// nothing is held between lines. A filter that remembered a table set by an
// earlier request would answer a request that never named one, and what this
// project tests is that a decoder uses the record's own codes against a table
// it was given.
//
// Values are read as whole tokens and a token with anything left in it is
// rejected, which is P18's lesson of Monday 26 October 2026. Floating point
// values go out as "%.17g", enough digits that every double reads back as the
// same bits; the Rust prints the shortest such form, so the two filters print
// different text for one value and the test compares numbers.

#include "recdecode.hpp"

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

bool read_long(std::istringstream &in, long low, long high, long &out)
{
    std::string token;
    if (!(in >> token)) {
        return false;
    }
    char *end = nullptr;
    const long v = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || v < low || v > high) {
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

// A count and then that many bytes, which is how a buffer shorter than the
// header crosses the protocol.
bool read_bytes(std::istringstream &in, std::vector<std::uint8_t> &out)
{
    out.clear();
    unsigned long want = 0u;
    if (!read_ulong(in, 4u * p14::kHdrBytes, want)) {
        return false;
    }
    for (unsigned long i = 0u; i < want; ++i) {
        unsigned long byte = 0u;
        if (!read_ulong(in, 255u, byte)) {
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(byte));
    }
    return true;
}

bool read_table(std::istringstream &in, p14::Table &out)
{
    for (std::size_t i = 0u; i < p14::kFsCodes; ++i) {
        if (!read_double(in, out.lo_mg_per_lsb[i])) {
            return false;
        }
    }
    for (std::size_t i = 0u; i < p14::kFsCodes; ++i) {
        if (!read_double(in, out.hi_mg_per_lsb[i])) {
            return false;
        }
    }
    for (std::size_t i = 0u; i < p14::kBaroCodes; ++i) {
        if (!read_double(in, out.baro_lsb_per_hpa[i])) {
            return false;
        }
    }
    unsigned long lo = 0u;
    unsigned long hi = 0u;
    unsigned long baro = 0u;
    if (!read_ulong(in, 255u, lo) || !read_ulong(in, 255u, hi)
        || !read_ulong(in, 255u, baro)) {
        return false;
    }
    out.lo_valid = static_cast<std::uint8_t>(lo);
    out.hi_valid = static_cast<std::uint8_t>(hi);
    out.baro_valid = static_cast<std::uint8_t>(baro);
    return true;
}

void refused(p14::Error e)
{
    std::printf("REFUSED %s\n", p14::name(e));
}

int do_header(std::istringstream &in)
{
    std::vector<std::uint8_t> buf;
    if (!read_bytes(in, buf)) {
        return -1;
    }
    p14::Header hdr;
    if (const auto bad = p14::parse_header(buf, hdr)) {
        refused(*bad);
        return 0;
    }
    std::printf("tick=%lu pre=%u post=%u baro=%u lo=%u hi=%u range=%u "
                "flags=%u\n",
                static_cast<unsigned long>(hdr.tick_event),
                static_cast<unsigned>(hdr.n_pre),
                static_cast<unsigned>(hdr.n_post),
                static_cast<unsigned>(hdr.n_baro),
                static_cast<unsigned>(hdr.lo_fs_g),
                static_cast<unsigned>(hdr.hi_fs_g),
                static_cast<unsigned>(hdr.baro_range),
                static_cast<unsigned>(hdr.flags));
    return 0;
}

int do_mg(std::istringstream &in)
{
    std::vector<std::uint8_t> buf;
    p14::Table table;
    long counts = 0;
    unsigned long fine = 0u;
    if (!read_bytes(in, buf) || !read_table(in, table)
        || !read_long(in, -32768, 32767, counts)
        || !read_ulong(in, 1u, fine)) {
        return -1;
    }
    p14::Header hdr;
    if (const auto bad = p14::parse_header(buf, hdr)) {
        refused(*bad);
        return 0;
    }
    double mg = 0.0;
    const auto bad = p14::counts_to_mg(hdr, table,
                                       static_cast<std::int16_t>(counts),
                                       fine != 0u, mg);
    if (bad) {
        refused(*bad);
        return 0;
    }
    std::printf("mg=%.17g\n", mg);
    return 0;
}

int do_hpa(std::istringstream &in)
{
    std::vector<std::uint8_t> buf;
    p14::Table table;
    long counts = 0;
    if (!read_bytes(in, buf) || !read_table(in, table)
        || !read_long(in, -2147483647L - 1L, 2147483647L, counts)) {
        return -1;
    }
    p14::Header hdr;
    if (const auto bad = p14::parse_header(buf, hdr)) {
        refused(*bad);
        return 0;
    }
    double hpa = 0.0;
    const auto bad = p14::counts_to_hpa(hdr, table,
                                        static_cast<std::int32_t>(counts),
                                        hpa);
    if (bad) {
        refused(*bad);
        return 0;
    }
    std::printf("hpa=%.17g\n", hpa);
    return 0;
}

int do_altitude(std::istringstream &in)
{
    double p = 0.0;
    double p0 = 0.0;
    if (!read_double(in, p) || !read_double(in, p0)) {
        return -1;
    }
    double m = 0.0;
    if (const auto bad = p14::altitude_m(p, p0, m)) {
        refused(*bad);
        return 0;
    }
    std::printf("m=%.17g\n", m);
    return 0;
}

int do_resolution(std::istringstream &in)
{
    double lsb = 0.0;
    double p = 0.0;
    double p0 = 0.0;
    if (!read_double(in, lsb) || !read_double(in, p) || !read_double(in, p0)) {
        return -1;
    }
    double m = 0.0;
    if (const auto bad = p14::altitude_resolution_m(lsb, p, p0, m)) {
        refused(*bad);
        return 0;
    }
    std::printf("m=%.17g\n", m);
    return 0;
}

int do_drain(std::istringstream &in)
{
    unsigned long samples = 0u;
    unsigned long per_sample = 0u;
    unsigned long overhead = 0u;
    double bus_hz = 0.0;
    if (!read_ulong(in, 0xFFFFFFFFu, samples)
        || !read_ulong(in, 0xFFFFFFFFu, per_sample)
        || !read_ulong(in, 0xFFFFFFFFu, overhead)
        || !read_double(in, bus_hz)) {
        return -1;
    }
    double ms = 0.0;
    const auto bad = p14::drain_ms(static_cast<std::uint32_t>(samples),
                                   static_cast<std::uint32_t>(per_sample),
                                   static_cast<std::uint32_t>(overhead),
                                   bus_hz, ms);
    if (bad) {
        refused(*bad);
        return 0;
    }
    std::printf("ms=%.17g\n", ms);
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
        if (verb == "H") {
            rc = do_header(in);
        } else if (verb == "M") {
            rc = do_mg(in);
        } else if (verb == "P") {
            rc = do_hpa(in);
        } else if (verb == "A") {
            rc = do_altitude(in);
        } else if (verb == "R") {
            rc = do_resolution(in);
        } else if (verb == "D") {
            rc = do_drain(in);
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
