// projects/P20-rtos-task-set/cpp/latstat.hpp: the latency histogram, its
// percentiles, the verdict against table 20.4 and the stack arithmetic, in
// C++17.
//
// The same arithmetic, in the same order, as `../c/latstat.c`,
// `../python/latstat.py` and `../rust/src/lib.rs`. The six findings this
// project is about are written out in `../c/latstat.h` and are not repeated
// here.
//
// WHAT C++ ADDS HERE, AND IT IS ONE THING WORTH HAVING. The answer varies by
// project in this volume and is worth keeping accurate. There are now five
// answers on record:
//
//   P18  one function template where the C needs two forty-line copies
//   P12  the C's lack of an allocator costs it sixty lines of fixed buffers,
//        bounds checks and an insertion sort the other three do not have
//   P17  almost nothing: four small functions over fixed-width integers
//   P16  a fourth independent algorithm for the one quantity where
//        implementations have real room to disagree
//   P20  std::optional turns FINDING THREE from a convention into a type
//
// FINDING THREE, IN THE TYPE SYSTEM. That finding is that a percentile whose
// rank falls inside the overflow count has no bin, and that reporting the top
// bin for it is the one wrong answer nothing downstream can detect. The C
// carries that as TWO FIELDS, `median_us` and `median_resolved`, which can
// disagree and which a caller can read in the wrong order: nothing stops a
// reader taking `median_us` without looking at the flag, and the value sitting
// there is zero, which reads as the fastest run ever measured.
//
// Here `bin_at_rank` returns `std::optional<std::uint32_t>` and the report
// holds optionals, so **the value cannot be read without the question being
// answered first**. That is not a better algorithm and it is not a smaller
// program; it is the difference between a convention and a thing the compiler
// enforces, on exactly the finding where a silent wrong answer was the risk.
//
// The two flat fields come back at the boundary, in the filter, because the
// oracle compares seven named numbers across four languages and an optional
// has no spelling in that protocol. So the guarantee holds where the
// arithmetic is and is given up where the wire format starts, which is the
// honest place to lose it.
//
// WHAT ELSE THE LANGUAGE GIVES, WHICH IS THE USUAL SHORT LIST.
// `std::optional<Error>` makes a refusal a different type rather than a
// negative return code, `std::array` fixes the 1024 bins without a macro, and
// `std::accumulate` sums them in one line. That is all of it.
//
// EVERY QUANTITY HERE IS AN INTEGER, so there is no floating point and no
// contraction flag applies, which is also why the build does not pass one.
#ifndef P20_LATSTAT_HPP
#define P20_LATSTAT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>

namespace p20 {

// Step 5's instrument: one bin per microsecond, and the chapter's number.
constexpr std::size_t kBins = 1024;

// Table 20.4's three latency rows, in microseconds, quoted and not chosen.
constexpr std::uint32_t kBudgetMedian = 100;
constexpr std::uint32_t kBudgetP999 = 250;
constexpr std::uint32_t kBudgetMax = 1000;

// The same six refusals as the C, in the same order, so a name means the same
// thing in all four implementations.
enum class Error {
    kArgument = 1,
    kRate,
    kFull,
    kEmpty,
    kCount,
    kStack,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kRate:     return "rate";
    case Error::kFull:     return "full";
    case Error::kEmpty:    return "empty";
    case Error::kCount:    return "count";
    case Error::kStack:    return "stack";
    }
    return "unknown";
}

// Three verdicts, and the third one is finding one.
enum class Verdict {
    kPass = 0,
    kFail = 1,
    kUndecided = 2,
};

// The C's 0, 1 and 2, so one oracle field serves all four languages.
inline std::uint8_t code(Verdict v)
{
    return static_cast<std::uint8_t>(v);
}

struct Hist {
    std::array<std::uint32_t, kBins> bins{};
    std::uint32_t over = 0;   // samples at or above kBins microseconds
    std::uint32_t max_us = 0; // running maximum, which the bins cannot hold
    std::uint32_t count = 0;  // every sample, including the ones in over
};

// The percentiles are optionals rather than a value and a flag, which is
// finding three in the type system. The flat pair is produced at the boundary
// by the filter and not carried here.
struct Report {
    std::optional<std::uint32_t> median_us;
    std::optional<std::uint32_t> p999_us;
    std::uint32_t max_us = 0;
    std::uint32_t over = 0;
    std::uint32_t count = 0;
};

struct Budget {
    std::uint32_t median_us = kBudgetMedian;
    std::uint32_t p999_us = kBudgetP999;
    std::uint32_t max_us = kBudgetMax;
};

struct Verdicts {
    Verdict median = Verdict::kUndecided;
    Verdict p999 = Verdict::kUndecided;
    Verdict max = Verdict::kUndecided;
};

struct Stack {
    std::uint32_t used = 0;   // the budget less the kernel's free figure
    std::uint32_t permil = 0; // used per thousand of the budget, truncated
    bool within = false;      // at or below eighty percent
};

// One sample. cycles_per_us scales the core's cycle counter, and the division
// TRUNCATES, which finding two is about.
inline std::optional<Error> record(Hist &h, std::uint32_t cycles,
                                   std::uint32_t cycles_per_us)
{
    if (cycles_per_us == 0u) {
        return Error::kRate;
    }
    // Refused before the sample is taken rather than after the counter wraps.
    // A wrapped count makes every rank below it wrong and nothing in the
    // printed result would show it.
    if (h.count == UINT32_MAX) {
        return Error::kFull;
    }

    const std::uint32_t us = cycles / cycles_per_us;
    if (us < kBins) {
        h.bins[us] += 1u;
    } else {
        h.over += 1u;
    }
    if (us > h.max_us) {
        h.max_us = us;
    }
    h.count += 1u;
    return std::nullopt;
}

// The ceiling of count times numer over denom, in 64 bits. Finding four: a
// 32-bit product wraps for a count this counter can hold and reports a
// percentile far below the truth.
inline std::uint32_t rank_of(std::uint32_t count, std::uint32_t numer,
                             std::uint32_t denom)
{
    const std::uint64_t product = static_cast<std::uint64_t>(count)
                                * static_cast<std::uint64_t>(numer);
    const std::uint64_t rank = (product + denom - 1u) / denom;
    return static_cast<std::uint32_t>(rank);
}

// The bin holding the sample of the given rank, counting from one. Empty when
// the rank falls inside the overflow count, which is finding three, and the
// return type is what stops a caller reading a value that is not there.
inline std::optional<std::uint32_t> bin_at_rank(const Hist &h,
                                                std::uint32_t rank)
{
    std::uint64_t seen = 0u;
    for (std::size_t k = 0u; k < kBins; ++k) {
        seen += static_cast<std::uint64_t>(h.bins[k]);
        if (seen >= static_cast<std::uint64_t>(rank)) {
            return static_cast<std::uint32_t>(k);
        }
    }
    return std::nullopt;
}

inline std::optional<Error> report(const Hist &h, Report &out)
{
    // A report of zero microseconds would read as the fastest run ever
    // measured, so no samples is a refusal and not a figure.
    if (h.count == 0u) {
        return Error::kEmpty;
    }

    // The count is checked against the histogram rather than trusted. One that
    // did not come from record gives ranks pointing at the wrong samples, and
    // nothing in the output would show it.
    const std::uint64_t total =
        std::accumulate(h.bins.begin(), h.bins.end(), std::uint64_t{0})
        + static_cast<std::uint64_t>(h.over);
    if (total != static_cast<std::uint64_t>(h.count)) {
        return Error::kCount;
    }

    // The rank definition is PINNED, because the chapter does not state one and
    // two reasonable choices differ by one sample. The percentile is the sample
    // of rank ceil(p * n), counting from one.
    out.median_us = bin_at_rank(h, rank_of(h.count, 1u, 2u));
    out.p999_us = bin_at_rank(h, rank_of(h.count, 999u, 1000u));
    out.max_us = h.max_us;
    out.over = h.over;
    out.count = h.count;
    return std::nullopt;
}

// Finding one: bin k covers the true values from k up to just under k plus
// one, so only a bin entirely on one side of the budget decides anything.
// Finding six: a STRICT comparison, because bin + 1 wrapped for a maximum at
// the top of a uint32_t and returned a pass for the worst possible run.
inline Verdict decide_bin(std::uint32_t bin, std::uint32_t budget)
{
    if (bin < budget) {
        return Verdict::kPass;
    }
    if (bin > budget) {
        return Verdict::kFail;
    }
    return Verdict::kUndecided;
}

// An unresolved percentile is a value at or above kBins microseconds, which is
// everything the histogram knows. That still fails any budget below kBins,
// which is where all three of table 20.4's rows sit.
inline Verdict decide_unresolved(std::uint32_t budget)
{
    return (kBins > budget) ? Verdict::kFail : Verdict::kUndecided;
}

inline Verdicts verdict(const Report &r, const Budget &b)
{
    Verdicts out;
    // The optional is what makes this read correctly: there is no way to reach
    // the value without having asked whether it exists.
    out.median = r.median_us ? decide_bin(*r.median_us, b.median_us)
                             : decide_unresolved(b.median_us);
    out.p999 = r.p999_us ? decide_bin(*r.p999_us, b.p999_us)
                         : decide_unresolved(b.p999_us);
    // The maximum is a recorded value and not a bin, but it was truncated by
    // the same division, so it is judged the same way.
    out.max = decide_bin(r.max_us, b.max_us);
    return out;
}

inline std::optional<Error> stack(std::uint32_t budget, std::uint32_t free,
                                  Stack &out)
{
    if (budget == 0u) {
        return Error::kStack;
    }
    // Finding five: more free space than the whole budget is impossible, so it
    // means the two numbers came from different tasks. Refused, not clamped.
    if (free > budget) {
        return Error::kStack;
    }
    out.used = budget - free;
    out.permil = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(out.used) * 1000u) / budget);
    // Eighty percent as a comparison and not a division, so the rule is exact
    // at the boundary: used over budget at or below four fifths.
    out.within = (static_cast<std::uint64_t>(out.used) * 5u)
              <= (static_cast<std::uint64_t>(budget) * 4u);
    return std::nullopt;
}

}  // namespace p20

#endif  // P20_LATSTAT_HPP
