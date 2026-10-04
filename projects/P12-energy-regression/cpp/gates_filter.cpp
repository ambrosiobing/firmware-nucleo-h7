// P12's filter: one case per line on stdin, one answer per line on stdout.
//
// The protocol is projects/P12-energy-regression/PROTOCOL.md. Two verbs:
//
//     S <measured> <budgets>
//     C <tolerance> <build> <total> <phases> <baseline>
//
// IT READS ALL OF STDIN RATHER THAN A LINE AT A TIME INTO A FIXED BUFFER, and
// that is a lesson rather than a preference. P08's filter used char[4096], met a
// request of twenty one thousand characters on Saturday 3 October 2026, and
// answered twenty requests with twenty five answers: it had split one long line
// into several and each fragment parsed into something. A filter that quietly
// turns one request into several is the worst failure mode available to it,
// because the parity test then compares misaligned answers and reports a
// disagreement about the gate.
//
// A malformed request is refused loudly on stdout and the process exits
// non-zero. It never guesses, and it never silently skips a line, because a
// skipped line also misaligns every answer after it.

#include "gates.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split(const std::string &text, char sep)
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

// A field is "-" for absent, which is a real state in both gates and not a
// formatting convenience.
bool absent(const std::string &field)
{
    return field == "-";
}

bool parse_long(const std::string &text, long &out)
{
    char *end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || (end != nullptr && *end != '\0')) {
        return false;
    }
    out = value;
    return true;
}

bool parse_double(const std::string &text, double &out)
{
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || (end != nullptr && *end != '\0')) {
        return false;
    }
    out = value;
    return true;
}

bool parse_sizes(const std::string &field, p12::SizeMap &out)
{
    if (absent(field)) {
        return true;
    }
    for (const std::string &item : split(field, ',')) {
        const std::vector<std::string> parts = split(item, ':');
        if (parts.size() != 3 || parts[0].empty()) {
            return false;
        }
        p12::Sizes sizes;
        if (!absent(parts[1])) {
            long value = 0;
            if (!parse_long(parts[1], value)) {
                return false;
            }
            sizes.flash = value;
        }
        if (!absent(parts[2])) {
            long value = 0;
            if (!parse_long(parts[2], value)) {
                return false;
            }
            sizes.static_ram = value;
        }
        out[parts[0]] = sizes;
    }
    return true;
}

bool parse_phases(const std::string &field, p12::PhaseMap &out)
{
    if (absent(field)) {
        return true;
    }
    for (const std::string &item : split(field, ',')) {
        const std::vector<std::string> parts = split(item, ':');
        if (parts.size() != 2 || parts[0].empty()) {
            return false;
        }
        double value = 0.0;
        if (!parse_double(parts[1], value)) {
            return false;
        }
        out[parts[0]] = value;
    }
    return true;
}

}  // namespace

int main()
{
    // All of stdin, then split on newlines. See the note at the top of the file.
    std::ostringstream all;
    all << std::cin.rdbuf();
    const std::string text = all.str();

    for (const std::string &raw : split(text, '\n')) {
        std::string line = raw;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        std::istringstream fields(line);
        std::string verb;
        fields >> verb;

        if (verb == "S") {
            std::string measured_field;
            std::string budgets_field;
            if (!(fields >> measured_field >> budgets_field)) {
                std::cout << "REFUSED malformed_size_request" << std::endl;
                return 2;
            }
            p12::SizeMap measured;
            p12::SizeMap budgets;
            if (!parse_sizes(measured_field, measured) ||
                !parse_sizes(budgets_field, budgets)) {
                std::cout << "REFUSED unparsable_sizes" << std::endl;
                return 2;
            }
            std::cout << p12::size_answer(measured, budgets) << "\n";
        } else if (verb == "C") {
            std::string tolerance_field;
            std::string build_field;
            std::string total_field;
            std::string phases_field;
            std::string baseline_field;
            if (!(fields >> tolerance_field >> build_field >> total_field
                         >> phases_field >> baseline_field)) {
                std::cout << "REFUSED malformed_charge_request" << std::endl;
                return 2;
            }
            double tolerance = 0.0;
            if (!parse_double(tolerance_field, tolerance)) {
                std::cout << "REFUSED unparsable_tolerance" << std::endl;
                return 2;
            }
            std::optional<double> total;
            if (!absent(total_field)) {
                double value = 0.0;
                if (!parse_double(total_field, value)) {
                    std::cout << "REFUSED unparsable_total" << std::endl;
                    return 2;
                }
                total = value;
            }
            p12::PhaseMap phases;
            p12::PhaseMap baseline;
            if (!parse_phases(phases_field, phases) ||
                !parse_phases(baseline_field, baseline)) {
                std::cout << "REFUSED unparsable_phases" << std::endl;
                return 2;
            }
            const std::string build = absent(build_field) ? std::string() : build_field;
            std::cout << p12::charge_answer(build, phases, total, baseline, tolerance)
                      << "\n";
        } else {
            std::cout << "REFUSED unknown_verb" << std::endl;
            return 2;
        }
    }

    std::cout.flush();
    return 0;
}
