// P19's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same three verbs as ../rust/src/main.rs, so one parity test
// drives both with the same requests:
//
//     M <addr> <len> <line>   -> base=.. end=.. bytes=.. lines=..
//                             head=0|1 tail=0|1
//     B <addr> <len> <line>   -> ok=1
//     R <base> <size>         -> field=N
//
// and any of them can answer REFUSED with one of the six names the C uses.
//
// EVERY VALUE IS AN UNSIGNED 32 BIT INTEGER IN DECIMAL, addresses included,
// which is P01's decision for register words applied again: hexadecimal would
// read better to a person and would add a second parser to every
// implementation for no gain.
//
// B answers ok=1 or a refusal and never ok=0: criterion 4 is a build time
// assertion, so the only two outcomes are that it holds or that the buffer is
// refused, and a filter printing a flag would invite a caller to read the flag
// instead of the refusal.
//
// A token with anything left in it is rejected, which is P18's lesson of Monday
// 26 October 2026, and the upper limit on each number is 0xFFFFFFFF rather than
// a smaller bound, because the top of the address space is exactly what finding
// four is about and a filter that refused to carry those values could not test
// it.

#include "mpualign.hpp"

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
    // strtoull and not strtoul: unsigned long is 32 bits on win11 aquamarine's
    // compiler and 64 on skyhorizon's, so the overflow of 4294967296 would be
    // detected on one laptop and not the other. The wider type makes the bound
    // below the only check, and it is the same check everywhere.
    const unsigned long long v = std::strtoull(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || v > 0xFFFFFFFFull) {
        return false;
    }
    out = static_cast<std::uint32_t>(v);
    return true;
}

int do_maint(std::istringstream &in)
{
    std::uint32_t addr = 0;
    std::uint32_t len = 0;
    std::uint32_t line = 0;
    if (!read_u32(in, addr) || !read_u32(in, len) || !read_u32(in, line)) {
        return -1;
    }
    p19::Range r;
    if (const auto bad = p19::maint_range(addr, len, line, r)) {
        std::printf("REFUSED %s\n", p19::name(*bad));
        return 0;
    }
    std::printf("base=%lu end=%lu bytes=%lu lines=%lu head=%u tail=%u\n",
                static_cast<unsigned long>(r.base),
                static_cast<unsigned long>(r.end),
                static_cast<unsigned long>(r.bytes),
                static_cast<unsigned long>(r.lines),
                r.head_shared ? 1u : 0u, r.tail_shared ? 1u : 0u);
    return 0;
}

int do_buffer(std::istringstream &in)
{
    std::uint32_t addr = 0;
    std::uint32_t len = 0;
    std::uint32_t line = 0;
    if (!read_u32(in, addr) || !read_u32(in, len) || !read_u32(in, line)) {
        return -1;
    }
    if (const auto bad = p19::buffer_check(addr, len, line)) {
        std::printf("REFUSED %s\n", p19::name(*bad));
        return 0;
    }
    std::printf("ok=1\n");
    return 0;
}

int do_region(std::istringstream &in)
{
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    if (!read_u32(in, base) || !read_u32(in, size)) {
        return -1;
    }
    std::uint8_t field = 0;
    if (const auto bad = p19::region_encode(base, size, field)) {
        std::printf("REFUSED %s\n", p19::name(*bad));
        return 0;
    }
    std::printf("field=%u\n", static_cast<unsigned>(field));
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
        if (verb == "M") {
            rc = do_maint(in);
        } else if (verb == "B") {
            rc = do_buffer(in);
        } else if (verb == "R") {
            rc = do_region(in);
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
