// test/cpp_filter.cpp: the C++ variant as a filter, driven by the Python tests.
//
// Reads one case per line from standard input:
//
//     version flags sequence feature battery
//
// and writes one line per case to standard output:
//
//     <10 hex digits of the encoded payload> <the same five fields decoded back>
//
// This is a deliberate change from the chapter, which sketched a C++ test that
// parses test/vectors.json. Parsing JSON in C++ with no dependency is a lot of
// code that tests nothing, and it would have limited the C++ comparison to the
// six vectors. Driving the variant from Python instead compares it against the C
// implementation over the whole 100000-case random run, which is the claim the
// acceptance criteria actually ask for, with less code in the way.
//
// Nothing in here is firmware. It exists so that two compilers can be asked the
// same question.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "payload.hpp"

int main()
{
    long long v = 0, f = 0, s = 0, ft = 0, b = 0;
    char line[256];

    while (std::fgets(line, sizeof line, stdin) != nullptr) {
        // A line that filled the buffer without reaching a newline is refused
        // rather than answered. fgets would otherwise return the rest of it as
        // a second request and this filter would answer both, which is how
        // P08's filter came to return 25 answers for 20 requests on Saturday
        // 3 October 2026. Every case this filter sees today is five short
        // integers, so the buffer is ample; refusing costs nothing and removes
        // the silent version of the failure.
        if (std::strchr(line, '\n') == nullptr && std::feof(stdin) == 0) {
            std::fprintf(stderr,
                         "a request line longer than %zu bytes was refused rather "
                         "than split\n",
                         sizeof line);
            return EXIT_FAILURE;
        }
        if (std::sscanf(line, "%lld %lld %lld %lld %lld", &v, &f, &s, &ft, &b) != 5) {
            continue;                      // blank or malformed line, skip it
        }
        const payload::Values in{
            static_cast<std::uint32_t>(v),
            static_cast<std::uint32_t>(f),
            static_cast<std::uint32_t>(s),
            static_cast<std::int32_t>(ft),
            static_cast<std::uint32_t>(b),
        };
        const payload::Buffer buf = payload::encode(in);
        for (std::size_t i = 0; i < payload::total_bytes; ++i) {
            std::printf("%02X", buf[i]);
        }
        const payload::Values out = payload::decode(buf);
        std::printf(" %u %u %u %d %u\n",
                    out.version, out.flags, out.sequence, out.feature, out.battery);
    }
    return EXIT_SUCCESS;
}
