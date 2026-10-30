// P15's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same one verb as ../rust/src/main.rs, so one parity test drives
// both with the same requests:
//
//     T <far> <enter> <leave> <confirm_n> <confirm_m> <n> <distance>*n
//         -> frames=N changes=N ready=0|1 states=clear,approaching,..
//         -> REFUSED policy
//
// THE POLICY TRAVELS IN EVERY REQUEST, because it is a parameter in all four
// implementations: the chapter's own note on figure 15.4 calls its three
// thresholds "a starting point measured against a real target rather than
// constants to be trusted", and six of the oracle's nineteen cases override
// them. A filter holding a policy between lines would answer a request that
// never named one.
//
// THE STATE AFTER EVERY FRAME IS REPORTED, as names, in one comma separated
// token, because a state machine that reaches the right end through the wrong
// middle is wrong and three of the cases exist to pin a middle.
//
// A refusal is answered for the whole request rather than per frame: the policy
// is validated on every update, so a policy that is refused is refused on the
// first frame and no state is produced at all.
//
// Every value here is an integer, so nothing in this protocol rounds and there
// is no printing decision to make about precision.

#include "zonetrack.hpp"

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

int do_track(std::istringstream &in)
{
    unsigned long far_mm = 0u;
    unsigned long enter_mm = 0u;
    unsigned long leave_mm = 0u;
    unsigned long confirm_n = 0u;
    unsigned long confirm_m = 0u;
    unsigned long n = 0u;
    if (!read_ulong(in, 65535u, far_mm) || !read_ulong(in, 65535u, enter_mm)
        || !read_ulong(in, 65535u, leave_mm)
        || !read_ulong(in, 255u, confirm_n)
        || !read_ulong(in, 255u, confirm_m) || !read_ulong(in, 100000u, n)) {
        return -1;
    }

    p15::Policy policy;
    policy.far_mm = static_cast<std::uint16_t>(far_mm);
    policy.enter_mm = static_cast<std::uint16_t>(enter_mm);
    policy.leave_mm = static_cast<std::uint16_t>(leave_mm);
    policy.confirm_n = static_cast<std::uint8_t>(confirm_n);
    policy.confirm_m = static_cast<std::uint8_t>(confirm_m);

    std::vector<std::uint16_t> distances;
    distances.reserve(n);
    for (unsigned long i = 0u; i < n; ++i) {
        unsigned long d = 0u;
        if (!read_ulong(in, 65535u, d)) {
            return -1;
        }
        distances.push_back(static_cast<std::uint16_t>(d));
    }

    p15::Tracker tracker;
    std::string names;
    for (const std::uint16_t d : distances) {
        p15::State state = p15::State::kClear;
        if (const auto bad = p15::update(tracker, policy, d, state)) {
            std::printf("REFUSED %s\n", p15::error_name(*bad));
            return 0;
        }
        if (!names.empty()) {
            names.push_back(',');
        }
        names += p15::name(state);
    }
    std::printf("frames=%lu changes=%lu ready=%u states=%s\n",
                static_cast<unsigned long>(tracker.frames),
                static_cast<unsigned long>(tracker.changes),
                tracker.ready ? 1u : 0u, names.c_str());
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
        if (verb != "T") {
            std::printf("BAD_VERB %s\n", verb.c_str());
            return 1;
        }
        if (do_track(in) != 0) {
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
