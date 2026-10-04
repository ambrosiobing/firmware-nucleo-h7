// P12's two gates in C++, header only.
//
// The same rules, in the same order, as ../c/gates.c and ../python/gates.py.
// PROTOCOL.md in this project is the contract all four obey and says why the
// order of the charge gate's refusals is part of the rules rather than an
// accident of how the code reads.
//
// WHAT C++ ADDS HERE, and it is more than it added to P06's witness but still
// not much. A verdict is a value rather than a pair of out-parameters, so the
// gate returns one and the filter prints it; `std::optional` makes "the build
// reported no static_ram" a different type from "the build reported zero", which
// in the C is a bool beside a long and relies on the reader checking the bool;
// and `std::map` gives the name ordering the rules require without a sort
// written out, which is the one place here where the C is longer for no gain.
//
// WHAT IT COSTS, said plainly. This file allocates. `std::string`, `std::map`
// and `std::vector` all do, and that is why the C and not this is the version
// that could compile for the target. The gate runs on a laptop or on the
// Raspberry Pi beside the board, so the cost is not one this project pays, but
// the asymmetry is real and the project README records it rather than implying
// the four implementations are interchangeable everywhere.

#ifndef P12_GATES_HPP
#define P12_GATES_HPP

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace p12 {

// The chapter's figure: a five percent charge regression must turn a build red.
constexpr double kChargeToleranceDefault = 0.05;

// One target's sizes. An absent optional is a field the build did not report at
// all, which is a different case from one it reported as zero.
struct Sizes {
    std::optional<long> flash;
    std::optional<long> static_ram;
};

using SizeMap  = std::map<std::string, Sizes>;   // ordered by name, as the rules need
using PhaseMap = std::map<std::string, double>;

// Seventeen significant digits round-trip a double exactly, and PROTOCOL.md says
// why the answer carries that rather than the two decimal places the gate prints
// for a human: the comparison is of the arithmetic, not of four decimal
// printers. snprintf and not a stream, so this is byte for byte what ../c/gates.c
// writes.
inline std::string g17(double value)
{
    char buf[48];
    const int wrote = std::snprintf(buf, sizeof buf, "%.17g", value);
    if (wrote < 0 || static_cast<size_t>(wrote) >= sizeof buf) {
        return std::string("nan");   // unreachable for a finite double
    }
    return std::string(buf, static_cast<size_t>(wrote));
}

inline std::string num(long value)
{
    return std::to_string(value);
}

// The canonical answer of PROTOCOL.md, assembled from its four fields so no
// caller has to remember the order or the separators.
inline std::string answer(bool failed, const std::string &refusal,
                          const std::vector<std::string> &failures,
                          const std::vector<std::string> &report)
{
    const auto join = [](const std::vector<std::string> &items) {
        if (items.empty()) {
            return std::string("-");
        }
        std::string out = items.front();
        for (size_t i = 1; i < items.size(); i++) {
            out += ",";
            out += items[i];
        }
        return out;
    };
    return std::string(failed ? "FAIL" : "PASS") + " " +
           (refusal.empty() ? "-" : refusal) + " " +
           join(failures) + " " + join(report);
}

// ------------------------------------------------------------- the size gate

inline std::string size_answer(const SizeMap &measured, const SizeMap &budgets)
{
    // A gate with no input at all has nothing to report, and says so rather than
    // passing: a build that produced no sizes is a build failure wearing a
    // passing gate's clothes.
    if (measured.empty()) {
        return answer(true, "no_sizes", {}, {});
    }

    std::vector<std::string> failures;
    std::vector<std::string> report;

    for (const auto &[target, got] : measured) {
        const auto found = budgets.find(target);
        if (found == budgets.end()) {
            failures.push_back("no_budget:" + target);
            continue;
        }
        const Sizes &want = found->second;

        // flash first and then static_ram, which is the order the other three
        // produce and therefore the order of the failures a reader sees.
        const std::pair<const char *, int> fields[2] = {{"flash", 0}, {"static_ram", 1}};
        for (const auto &[fname, which] : fields) {
            const std::optional<long> &used_opt = which == 0 ? got.flash : got.static_ram;
            const std::optional<long> &limit_opt = which == 0 ? want.flash : want.static_ram;

            if (!used_opt.has_value()) {
                failures.push_back(std::string("no_field:") + target + ":" + fname);
                continue;
            }
            if (!limit_opt.has_value()) {
                failures.push_back(std::string("no_field_budget:") + target + ":" + fname);
                continue;
            }
            const long used = *used_opt;
            const long limit = *limit_opt;
            const double pct = (limit != 0)
                ? (100.0 * static_cast<double>(used) / static_cast<double>(limit))
                : 0.0;
            report.push_back(target + ":" + fname + ":" + num(used) + ":" +
                             num(limit) + ":" + g17(pct) + ":" + num(limit - used));
            if (used > limit) {
                failures.push_back(std::string("over:") + target + ":" + fname + ":" +
                                   num(used) + ":" + num(limit) + ":" + num(used - limit));
            }
        }
    }

    return answer(!failures.empty(), "", failures, report);
}

// ----------------------------------------------------------- the charge gate

inline std::string charge_answer(const std::string &build,
                                 const PhaseMap &phases,
                                 const std::optional<double> &total_uc,
                                 const PhaseMap &baseline,
                                 double tolerance = kChargeToleranceDefault)
{
    // The four refusals in the order PROTOCOL.md fixes. Build identity first: a
    // capture that cannot be tied to a firmware is not evidence whatever its
    // numbers say, so there is no point examining them.
    if (build.empty()) {
        return answer(true, "no_build", {}, {});
    }
    if (phases.empty() || baseline.empty()) {
        return answer(true, "no_phases", {}, {});
    }
    if (!total_uc.has_value()) {
        return answer(true, "no_total", {}, {});
    }

    // Summed in name order, because std::map iterates in name order and the
    // other three sort before summing. Addition is not associative in floating
    // point, and this is the one sum whose order is visible in the answer.
    double summed = 0.0;
    for (const auto &[name, value] : phases) {
        (void) name;
        summed += value;
    }

    // A run whose parts do not reconcile has lost or double counted a phase, and
    // no verdict on it is worth anything. Before any phase is compared, for
    // exactly that reason.
    const double slack = std::max(1.0, 0.05 * *total_uc);
    if (std::fabs(summed - *total_uc) > slack) {
        return answer(true, "sum_mismatch:" + g17(summed) + ":" + g17(*total_uc), {}, {});
    }

    std::vector<std::string> failures;
    std::vector<std::string> report;

    // The baseline's phases first. A phase the baseline watches and the ledger
    // does not report is a failure and not a pass: losing a phase looks like an
    // improvement, which is the direction that flatters the work.
    for (const auto &[name, want] : baseline) {
        const auto found = phases.find(name);
        if (found == phases.end()) {
            failures.push_back("missing_phase:" + name);
            continue;
        }
        const double got = found->second;
        const double change = (want != 0.0) ? (100.0 * (got - want) / want) : 0.0;
        report.push_back(name + ":" + g17(got) + ":" + g17(want) + ":" + g17(change));
        if (got > want * (1.0 + tolerance)) {
            failures.push_back("over:" + name + ":" + g17(got) + ":" + g17(want) +
                               ":" + g17(change));
        }
    }

    // Then the measured phases. One the baseline does not carry is a failure, or
    // the gate would be watching less than the run does.
    for (const auto &[name, got] : phases) {
        (void) got;
        if (baseline.find(name) == baseline.end()) {
            failures.push_back("not_in_baseline:" + name);
        }
    }

    return answer(!failures.empty(), "", failures, report);
}

}  // namespace p12

#endif  // P12_GATES_HPP
