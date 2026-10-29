// P10's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same one verb as ../rust/src/main.rs, so one parity test drives
// both with the same requests:
//
//     L <dt_s> <min_samples> <n> <current code>*n
//         -> total=.. split=.. residual=.. runs=.. rejected=..
//            rejected_samples=.. per0=.. per1=.. .. per7=..
//
// ALL EIGHT PER-CODE CHARGES ARE PRINTED, ZEROS INCLUDED, because chapter 10's
// codes 5 and 6 are unused and a trace that contains one is a finding the
// report has to be able to show. Printing only the non-zero ones would hide
// exactly the case the field exists for.
//
// EVERY FLOATING POINT VALUE CROSSES THIS PROTOCOL EXACTLY. The parity test
// compares the total and the eight charges with no tolerance, so a value losing
// one bit on the wire would fail a comparison the arithmetic passed. Values are
// read with strtod, which is correctly rounded, and printed with "%.17g", which
// is enough decimal digits that every double reads back as the same bits. The
// Rust reads with str::parse and prints with {:?}, the shortest form with the
// same guarantee, so the two filters print different text for one value and the
// test compares numbers rather than strings.
//
// A TOKEN WITH ANYTHING LEFT IN IT IS REJECTED, which is P18's lesson of Monday
// 26 October 2026: `in >> v` on a double cannot parse "inf" or "nan" in
// libstdc++, and a protocol that cannot carry a value the implementation would
// refuse cannot test the refusal. This project's own refusals include an
// interval of zero and a negative one, both of which strtod reads exactly.
//
// std::get_if AND NOT std::get, because this file is compiled with
// -fno-exceptions and std::get on the wrong alternative would reach
// std::terminate. The header says more about that choice.

#include "phaseledger.hpp"

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

int do_ledger(std::istringstream &in)
{
    double dt_s = 0.0;
    unsigned long min_samples = 0u;
    unsigned long n = 0u;
    if (!read_double(in, dt_s) || !read_ulong(in, 100000u, min_samples)
        || !read_ulong(in, 100000u, n)) {
        return -1;
    }

    std::vector<double> current;
    std::vector<std::uint8_t> codes;
    current.reserve(n);
    codes.reserve(n);
    for (unsigned long i = 0u; i < n; ++i) {
        double amps = 0.0;
        unsigned long code = 0u;
        if (!read_double(in, amps) || !read_ulong(in, 255u, code)) {
            return -1;
        }
        current.push_back(amps);
        codes.push_back(static_cast<std::uint8_t>(code));
    }

    // An empty trace is the argument refusal, and the two null pointers here
    // are how this implementation reaches it: a vector of nothing has no data
    // to point at, which is the same case the C reaches through a null
    // argument and the Rust through an empty slice.
    const double *amps_at = current.empty() ? nullptr : current.data();
    const std::uint8_t *codes_at = codes.empty() ? nullptr : codes.data();
    const p10::Answer answer =
        p10::phase_ledger(amps_at, codes_at, static_cast<std::size_t>(n), dt_s,
                          static_cast<std::uint32_t>(min_samples));

    if (const p10::Error *bad = std::get_if<p10::Error>(&answer)) {
        std::printf("REFUSED %s\n", p10::name(*bad));
        return 0;
    }
    const p10::Ledger *l = std::get_if<p10::Ledger>(&answer);
    if (l == nullptr) {
        // Unreachable: a variant of two alternatives holding neither is not a
        // state this type can be in. Written rather than assumed, because the
        // alternative is dereferencing a pointer on the strength of an
        // argument about a type.
        return -1;
    }

    std::printf("total=%.17g split=%.17g residual=%.17g runs=%lu rejected=%lu "
                "rejected_samples=%lu",
                l->total, l->split, l->residual,
                static_cast<unsigned long>(l->runs),
                static_cast<unsigned long>(l->rejected),
                static_cast<unsigned long>(l->rejected_samples));
    for (std::size_t c = 0u; c < p10::kCodes; ++c) {
        std::printf(" per%lu=%.17g", static_cast<unsigned long>(c),
                    l->per_code[c]);
    }
    std::printf("\n");
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
        if (verb != "L") {
            std::printf("BAD_VERB %s\n", verb.c_str());
            return 1;
        }
        if (do_ledger(in) != 0) {
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
