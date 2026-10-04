// P01's filter: one request per line on stdin, one answer per line on stdout.
//
// The protocol is ../PROTOCOL.md, and the Rust filter beside this one speaks it
// too, which is what lets one parity test drive both with the same code. Two
// verbs:
//
//     D <cr> <cfgr> <pllckselr> <pllcfgr> <pll1divr> <cdcfgr1> <cdcfgr2> <hsi> <hse>
//     B <reported> <true>
//
// IT READS ALL OF STDIN RATHER THAN A LINE AT A TIME INTO A FIXED BUFFER, which
// is a lesson and not a preference. P08's filter used char[4096], met a request
// of twenty one thousand characters on Saturday 3 October 2026, and answered
// twenty requests with twenty five answers: it had split one long line into
// several and each fragment parsed into something. A filter that quietly turns
// one request into several is the worst failure available to it, because the
// parity test then reports a disagreement about the subject when the fault was
// the parser.
//
// A malformed request is refused loudly on stdout and the process exits
// non-zero. It never guesses and it never silently skips a line, because a
// skipped line misaligns every answer after it.

#include "clocktree.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_on(const std::string &text, char sep)
{
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == sep) {
            out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    out.push_back(current);
    return out;
}

// Every field in this protocol is a decimal integer that fits in 32 bits
// unsigned. strtoull and a range check rather than strtoul, because strtoul on a
// 32-bit long would saturate at 0xFFFFFFFF and report success, turning an
// out-of-range register value into a plausible one.
bool parse_u32(const std::string &text, std::uint32_t &out)
{
    if (text.empty()) {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno != 0) {
        return false;
    }
    if (value > 0xFFFFFFFFull) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool answer_decode(const std::vector<std::string> &f, std::string &out)
{
    if (f.size() != 10) {
        return false;
    }
    std::uint32_t v[9];
    for (std::size_t i = 0; i < 9; ++i) {
        if (!parse_u32(f[i + 1], v[i])) {
            return false;
        }
    }
    const p01::Regs regs{v[0], v[1], v[2], v[3], v[4], v[5], v[6]};
    const p01::Tree tree = p01::decode(regs, v[7], v[8]);

    out = std::to_string(tree.sys_hz) + " " + std::to_string(tree.core_hz) + " "
        + std::to_string(tree.ahb_hz) + " " + std::to_string(tree.pclk1_hz) + " "
        + std::string(p01::refusal_text(tree.refusal));
    return true;
}

bool answer_bias(const std::vector<std::string> &f, std::string &out)
{
    if (f.size() != 3) {
        return false;
    }
    std::uint32_t reported = 0;
    std::uint32_t truth = 0;
    if (!parse_u32(f[1], reported) || !parse_u32(f[2], truth)) {
        return false;
    }
    const p01::Bias b = p01::bias(reported, truth);
    out = std::string(b.ok ? "1" : "0") + " " + std::to_string(b.frequency_ppm)
        + " " + std::to_string(b.duration_ppm) + " " + std::to_string(b.delay_ppm);
    return true;
}

}  // namespace

int main()
{
    std::string all;
    {
        std::string chunk;
        while (std::getline(std::cin, chunk)) {
            all += chunk;
            all += '\n';
        }
    }

    const std::vector<std::string> lines = split_on(all, '\n');
    for (const std::string &line : lines) {
        if (line.empty()) {
            // Only the trailing fragment after the final newline. Any blank line
            // in the middle would be a request this filter cannot answer, and
            // split_on puts the trailing one last, so skipping an empty line
            // here cannot misalign anything.
            continue;
        }
        const std::vector<std::string> fields = split_on(line, ' ');
        std::string out;
        bool ok = false;
        if (!fields.empty() && fields[0] == "D") {
            ok = answer_decode(fields, out);
        } else if (!fields.empty() && fields[0] == "B") {
            ok = answer_bias(fields, out);
        }
        if (!ok) {
            std::cout << "BAD REQUEST: " << line << std::endl;
            return 1;
        }
        std::cout << out << "\n";
    }
    std::cout.flush();
    return 0;
}
