// projects/P07-stop-mode/cpp/cyclecharge.hpp: the cycle segmentation, the
// report and the battery arithmetic in C++17.
//
// The same arithmetic, in the same order, as `../c/cyclecharge.c`,
// `../python/cyclecharge.py` and `../rust/src/lib.rs`. The file
// `../c/cyclecharge.h` carries the reasoning, including finding one: a trace
// that begins with the marker already high has no rising edge in front of its
// first cycle, so that cycle is not complete and is not reported.
//
// EVERY COMPARISON IN THIS PROJECT IS EXACT, WHICH GOVERNS THIS FILE. The
// oracle's values are powers of two and the parity test compares double with
// ==, with no tolerance anywhere. So the order of operations is part of the
// specification rather than a detail, and each one below is the C's:
//
//   a cycle's charge   the samples accumulated and THEN divided, sum / fs_hz
//   the spread         100.0 * (max - min) / median, in that grouping
//   the cell energy    cell_mah * 1e-3 * 3600.0 * cell_volts, left to right
//   an even median     0.5 * (a + b), which the chapter's own library does
//
// WHAT C++ ADDS HERE IS P12'S ANSWER A SECOND TIME, WHICH IS WHY IT IS WORTH
// SAYING. The volume's answers so far:
//
//   P18  one function template where the C needs two forty-line copies
//   P12  the C's lack of an allocator costs it sixty lines of fixed buffers,
//        bounds checks and an insertion sort
//   P17  almost nothing: four small functions over fixed-width integers
//   P16  a fourth independent algorithm for the one quantity where
//        implementations have real room to disagree
//   P20  std::optional turns finding three from a convention into a type
//   P04  std::variant reaches the same guarantee the Rust gets from an enum,
//        and charges more for it
//   P07  an allocator, again: a parameter pair and a refusal disappear
//
// THE PARAMETER PAIR AND THE REFUSAL. The C's per-cycle charge takes
// `double *out_uc, uint32_t max_out, uint32_t *count`: three parameters for one
// answer, because the caller owns the buffer. It therefore needs CYC_ERR_ROOM,
// a refusal that exists only because the caller might not have supplied enough
// space. Here the answer is a `std::vector<double>` the function fills, the
// count is its size, and **kRoom is unreachable in this function**. The enum
// value stays so the seven refusal names mean the same thing in all four
// implementations, and `cyc_report` still uses it for a different reason: a
// count above the bound is a caller's mistake about the data rather than about
// a buffer.
//
// That is not a new observation, it is P12's. One instance was a remark about
// one gate; two make it the thing C++ reliably buys on this part, and the
// pattern is the same both times: the refusal that disappears was never about
// the problem, it was about the C's inability to return a variable amount.
//
// AND IT STILL NEEDS -ffp-contract=off, WHICH THE RUST DOES NOT. gcc at -O2
// fuses a multiply followed by an add, rounding once where two operations round
// twice, and this file is compared bit for bit against twins that cannot fuse.
// The flag is in build_host.py and in code.yml, and
// python/tests/test_float_builds_disable_contraction.py enforces it, reading
// this header as well as the source since Wednesday 28 October 2026. The Rust
// needs no such flag because fusing there is `mul_add` and nothing else, which
// is what `../rust/README.md` is about.
#ifndef P07_CYCLECHARGE_HPP
#define P07_CYCLECHARGE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace p07 {

constexpr std::size_t kMaxCycles = 64;
constexpr std::uint32_t kMinCycles = 10;
constexpr double kSecondsPerDay = 86400.0;

// The same seven refusals as the C, in the same order.
enum class Error {
    kArgument = 1,
    kRate,
    kBit,
    kRoom,
    kCycles,
    kSupply,
    kDay,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kRate:     return "rate";
    case Error::kBit:      return "bit";
    case Error::kRoom:     return "room";
    case Error::kCycles:   return "cycles";
    case Error::kSupply:   return "supply";
    case Error::kDay:      return "day";
    }
    return "unknown";
}

struct Report {
    std::uint32_t cycles = 0;
    double median_uc = 0.0;
    double min_uc = 0.0;
    double max_uc = 0.0;
    double spread_pct = 0.0;
    double energy_median_uj = 0.0;
};

// Every assumption is a field, which is the chapter's own framing: only the
// first of these is a measurement.
struct Battery {
    double energy_per_cycle_uj = 0.0;
    double cycles_per_day = 0.0;
    double cycle_period_s = 0.0;
    double sleep_ua = 0.0;
    double sleep_volts = 0.0;
    double cell_mah = 0.0;
    double cell_volts = 0.0;
};

struct Lifetime {
    double cell_j = 0.0;
    double covered_s = 0.0;
    double uncovered_s = 0.0;
    double cycle_j_per_day = 0.0;
    double uncovered_j_per_day = 0.0;
    double days = 0.0;
    double days_chapter = 0.0;
    bool covers_the_day = false;
};

// The C's !(x > 0.0), which refuses zero, negatives and any value that is not a
// number, and accepts an infinite one. Written out so the three implementations
// agree on the infinite case too, which the oracle does not reach and which it
// would be a divergence to refuse here alone.
inline bool not_above_zero(double x)
{
    return !(x > 0.0);
}

// Charge in microcoulombs for each COMPLETE cycle, where a cycle runs from one
// rising edge of the marker bit to the next. The answer is the vector's size,
// so the C's max_out and count parameters are absent and so is its ROOM
// refusal.
inline std::optional<Error> per_cycle_charge(const double *current_ua,
                                             const std::uint8_t *digital,
                                             std::size_t n, double fs_hz,
                                             std::uint8_t cycle_bit,
                                             std::vector<double> &out)
{
    out.clear();
    if (current_ua == nullptr || digital == nullptr || n == 0u) {
        return Error::kArgument;
    }
    if (not_above_zero(fs_hz)) {
        return Error::kRate;
    }
    if (cycle_bit > 7u) {
        return Error::kBit;
    }

    std::size_t previous_start = 0u;
    bool have_start = false;
    for (std::size_t i = 1u; i < n; ++i) {
        // Cast to unsigned before the shift, exactly as the C does. Without
        // it the byte promotes to int, the mask makes it unsigned again, and
        // -Wsign-conversion is in this repository's warning set with -Werror.
        const unsigned was =
            (static_cast<unsigned>(digital[i - 1u]) >> cycle_bit) & 1u;
        const unsigned now =
            (static_cast<unsigned>(digital[i]) >> cycle_bit) & 1u;
        if (was != 0u || now == 0u) {
            continue;
        }
        if (have_start) {
            // Accumulated and THEN divided, which is the grouping the exact
            // comparison depends on.
            double sum = 0.0;
            for (std::size_t k = previous_start; k < i; ++k) {
                sum += current_ua[k];
            }
            out.push_back(sum / fs_hz);
        }
        previous_start = i;
        have_start = true;
    }
    return std::nullopt;
}

// The mean of the two middle values on an even count, which is what the
// chapter's library does and therefore what a comparison against it needs.
inline double median_of(const std::vector<double> &sorted)
{
    const std::size_t count = sorted.size();
    if ((count & 1u) != 0u) {
        return sorted[count / 2u];
    }
    return 0.5 * (sorted[count / 2u - 1u] + sorted[count / 2u]);
}

// The median, the extremes and the spread, refusing fewer cycles than the floor
// because the chapter's instruction to report at least ten is a rule.
inline std::optional<Error> report(const std::vector<double> &charge_uc,
                                   double volts, std::uint32_t min_cycles,
                                   Report &out)
{
    const std::size_t count = charge_uc.size();
    if (count > kMaxCycles) {
        return Error::kRoom;
    }
    if (not_above_zero(volts)) {
        return Error::kSupply;
    }
    if (count < min_cycles || count == 0u) {
        return Error::kCycles;
    }

    // std::sort rather than the C's insertion sort, which is the one place this
    // file does not mirror the algorithm. The two produce the same sorted
    // sequence for any set of numbers, and only the order matters to everything
    // below, so the exact comparison is unaffected. It would be affected by a
    // value that is not a number, which no oracle case holds and which the C's
    // own insertion sort orders arbitrarily too.
    std::vector<double> sorted(charge_uc);
    std::sort(sorted.begin(), sorted.end());

    out.cycles = static_cast<std::uint32_t>(count);
    out.min_uc = sorted.front();
    out.max_uc = sorted.back();
    out.median_uc = median_of(sorted);
    out.energy_median_uj = out.median_uc * volts;
    if (out.median_uc == 0.0) {
        // Every cycle drew nothing, so the spread has no denominator. Reported
        // as zero rather than as a division, and the medians beside it say why.
        out.spread_pct = 0.0;
    } else {
        out.spread_pct = 100.0 * (out.max_uc - out.min_uc) / out.median_uc;
    }
    return std::nullopt;
}

// Arithmetic only, with every assumption an argument and the uncovered part of
// the day costed rather than free.
inline std::optional<Error> lifetime(const Battery &in, Lifetime &out)
{
    if (not_above_zero(in.energy_per_cycle_uj) || not_above_zero(in.cell_mah)
        || not_above_zero(in.cell_volts) || not_above_zero(in.sleep_volts)
        || not_above_zero(in.cycle_period_s)
        || not_above_zero(in.cycles_per_day)) {
        return Error::kSupply;
    }
    if (in.sleep_ua < 0.0) {
        return Error::kSupply;
    }

    out.covered_s = in.cycles_per_day * in.cycle_period_s;
    if (out.covered_s > kSecondsPerDay) {
        // More cycling than there is day to cycle in. Refused rather than
        // reported, because the rest of this sum would be about a day that does
        // not exist.
        return Error::kDay;
    }
    out.uncovered_s = kSecondsPerDay - out.covered_s;
    out.covers_the_day = (out.uncovered_s == 0.0);

    out.cell_j = in.cell_mah * 1e-3 * 3600.0 * in.cell_volts;
    out.cycle_j_per_day = in.energy_per_cycle_uj * 1e-6 * in.cycles_per_day;
    out.uncovered_j_per_day = in.sleep_ua * 1e-6 * in.sleep_volts
                            * out.uncovered_s;

    // Step 10's printed form, which divides by the cycles alone.
    out.days_chapter = out.cell_j / out.cycle_j_per_day;
    const double total = out.cycle_j_per_day + out.uncovered_j_per_day;
    out.days = out.cell_j / total;
    return std::nullopt;
}

}  // namespace p07

#endif  // P07_CYCLECHARGE_HPP
