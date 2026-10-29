// P07's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same three verbs as ../rust/src/main.rs, so one parity test
// drives both with the same code:
//
//     C <fs_hz> <cycle_bit> <max_out> <n> <current digital>*n
//     R <volts> <min_cycles> <count> <count charges>
//     L <energy_uj> <cycles_per_day> <period_s> <sleep_ua> <sleep_v> <mah> <cell_v>
//
// EVERY FLOATING POINT VALUE CROSSES THIS PROTOCOL EXACTLY, which matters more
// here than in any other filter in this volume: P07's parity test compares
// double with == and no tolerance, so a value losing one bit on the wire would
// fail a comparison the arithmetic passed. Values are read with strtod, which
// is correctly rounded, and printed with "%.17g", which is enough decimal
// digits that every double reads back as the same bits. Python's repr and float
// are the same pair of guarantees on the other side.
//
// MAX_OUT IS READ AND IGNORED, AND SAYING SO IS THE POINT. The C needs it,
// because its caller owns the buffer; this implementation returns a vector and
// has no buffer to overrun, so its ROOM refusal is unreachable. The token stays
// in the protocol because one test drives four implementations and the request
// has to be the same for all of them. The header says what disappears and why.
//
// Values are read as tokens and a token with anything left in it is rejected,
// which is P18's lesson of Monday 26 October 2026: `in >> v` on a double cannot
// parse "inf" or "nan" in libstdc++, and a protocol that cannot carry a value
// the implementation would reject cannot test the rejection. This project's
// oracle has no such case and the habit costs nothing.

#include "cyclecharge.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

void refused(p07::Error e)
{
    std::printf("REFUSED %s\n", p07::name(e));
}

// Enough digits that every double reads back as the same bits.
void print_double(const char *key, double value, bool last)
{
    std::printf("%s=%.17g%s", key, value, last ? "\n" : " ");
}

int do_charge(std::istringstream &in)
{
    double fs_hz = 0.0;
    unsigned long cycle_bit = 0u;
    unsigned long max_out = 0u;
    unsigned long n = 0u;
    if (!read_double(in, fs_hz) || !read_ulong(in, 255u, cycle_bit)
        || !read_ulong(in, 100000u, max_out)
        || !read_ulong(in, 100000u, n)) {
        return -1;
    }

    std::vector<double> current;
    std::vector<std::uint8_t> digital;
    current.reserve(n);
    digital.reserve(n);
    for (unsigned long i = 0u; i < n; ++i) {
        double amps = 0.0;
        unsigned long bit = 0u;
        if (!read_double(in, amps) || !read_ulong(in, 255u, bit)) {
            return -1;
        }
        current.push_back(amps);
        digital.push_back(static_cast<std::uint8_t>(bit));
    }
    (void) max_out;  // read for the protocol, unused here; the header says why

    std::vector<double> charges;
    const double *amps_at = current.empty() ? nullptr : current.data();
    const std::uint8_t *bits_at = digital.empty() ? nullptr : digital.data();
    if (const auto bad = p07::per_cycle_charge(amps_at, bits_at,
                                               static_cast<std::size_t>(n),
                                               fs_hz,
                                               static_cast<std::uint8_t>(cycle_bit),
                                               charges)) {
        refused(*bad);
        return 0;
    }
    std::printf("count=%lu uc=", static_cast<unsigned long>(charges.size()));
    for (std::size_t i = 0u; i < charges.size(); ++i) {
        std::printf("%s%.17g", i > 0u ? "," : "", charges[i]);
    }
    std::printf("\n");
    return 0;
}

int do_report(std::istringstream &in)
{
    double volts = 0.0;
    unsigned long min_cycles = 0u;
    unsigned long count = 0u;
    if (!read_double(in, volts) || !read_ulong(in, 100000u, min_cycles)
        || !read_ulong(in, 100000u, count)) {
        return -1;
    }
    std::vector<double> charges;
    charges.reserve(count);
    for (unsigned long i = 0u; i < count; ++i) {
        double v = 0.0;
        if (!read_double(in, v)) {
            return -1;
        }
        charges.push_back(v);
    }

    p07::Report r;
    if (const auto bad = p07::report(charges, volts,
                                     static_cast<std::uint32_t>(min_cycles),
                                     r)) {
        refused(*bad);
        return 0;
    }
    std::printf("cycles=%lu ", static_cast<unsigned long>(r.cycles));
    print_double("median", r.median_uc, false);
    print_double("min", r.min_uc, false);
    print_double("max", r.max_uc, false);
    print_double("spread", r.spread_pct, false);
    print_double("energy", r.energy_median_uj, true);
    return 0;
}

int do_lifetime(std::istringstream &in)
{
    p07::Battery b;
    double *fields[] = {&b.energy_per_cycle_uj, &b.cycles_per_day,
                        &b.cycle_period_s, &b.sleep_ua, &b.sleep_volts,
                        &b.cell_mah, &b.cell_volts};
    for (double *field : fields) {
        if (!read_double(in, *field)) {
            return -1;
        }
    }

    p07::Lifetime l;
    if (const auto bad = p07::lifetime(b, l)) {
        refused(*bad);
        return 0;
    }
    print_double("cell_j", l.cell_j, false);
    print_double("covered", l.covered_s, false);
    print_double("uncovered", l.uncovered_s, false);
    print_double("cycle_j", l.cycle_j_per_day, false);
    print_double("uncovered_j", l.uncovered_j_per_day, false);
    print_double("days", l.days, false);
    print_double("days_chapter", l.days_chapter, false);
    std::printf("covers=%u\n", l.covers_the_day ? 1u : 0u);
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
            rc = do_charge(in);
        } else if (verb == "R") {
            rc = do_report(in);
        } else if (verb == "L") {
            rc = do_lifetime(in);
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
