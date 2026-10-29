// projects/P10-energy-phases/cpp/phaseledger.hpp: the per-phase charge, the
// total integrated by the other route, and the residual between them, in C++17.
//
// The same arithmetic, in the same order, as `../c/phaseledger.c`,
// `../python/phaseledger.py` and `../rust/src/lib.rs`. The file
// `../c/phaseledger.h` carries the reasoning and is not repeated here,
// including the two that matter most: the total is integrated WITH NO
// REFERENCE TO THE MARKERS AT ALL, because a residual computed between the
// phase sum and itself would always be zero and would prove nothing, and the
// runs are cut by INDEX rather than by time, which is the whole correction this
// project makes to the chapter's sketch.
//
// THE ORDER OF OPERATIONS IS PART OF THE SPECIFICATION HERE, not a detail. The
// oracle's currents are sums of powers of two with dt at 2^-10 seconds and the
// parity test compares `double` with `==` for the total and for all eight
// per-code charges, with no tolerance at all. Each grouping below is the C's,
// mirrored rather than rederived:
//
//   the total        every sample accumulated and THEN multiplied by dt
//   a run's charge   that run's samples accumulated and THEN multiplied by dt
//   the residual     |total - split| / |total|, and 1.0 when total is zero
//
// WHAT C++ ADDS HERE IS P04'S ANSWER A SECOND TIME, which is the point of
// asking the question once per project. The volume's answers so far:
//
//   P18  one function template where the C needs two forty-line copies
//   P12  the C's lack of an allocator costs it sixty lines of fixed buffers,
//        bounds checks and an insertion sort
//   P17  almost nothing: four small functions over fixed-width integers
//   P16  a fourth independent algorithm for the one quantity where
//        implementations have real room to disagree
//   P20  std::optional turns finding three from a convention into a type
//   P04  std::variant reaches the same guarantee the Rust gets from an enum
//   P07  an allocator, again: a parameter pair and a refusal disappear
//   P10  std::variant, again, and this time on a promise the C's own header
//        makes in prose
//
// THE PROMISE. `../c/phaseledger.h` says of its return value: "Returns 0, or
// one of the refusals above having written nothing." The C keeps that promise
// by validating every argument before it writes to `*out`, which is discipline
// rather than a guarantee: nothing in `int phase_ledger(..., phase_ledger_t
// *out)` prevents a future edit from filling half the struct and then
// refusing, and a caller that ignored the return value would read those half
// written fields. Here the answer is a `std::variant<Ledger, Error>` returned
// by value, so **a refusal carries no ledger to read**: the promise is the
// type. That is P04's answer, where a delivery of nothing, one span or two
// spans became a sum type instead of a count plus two maybe-valid structs.
//
// It is a smaller gain than P04's and that is worth saying rather than
// inflating. P04 had three shapes of answer and a real chance of reading a span
// that was never written; this has one shape and one unwritten struct. What is
// the same in both is the mechanism: **the C's out-parameter makes the invalid
// state representable and the sum type does not.**
//
// AND NO ALLOCATOR APPEARS, WHICH IS THE OTHER HALF OF AN HONEST ANSWER. P12
// and P07 both bought sixty lines and a refusal with a std::vector, because
// their answers are a variable number of values. This project's answer is eight
// charges, one per marker code, however long the trace: a fixed size, so there
// is nothing for an allocator to do and `std::array<double, kCodes>` is the
// whole of it. What that array does buy is small and real: the size is part of
// the type, so the loop that clears it and the loop that reads it cannot drift
// apart from the declaration the way a `double[8]` and a separate `PHASE_CODES`
// can.
//
// std::get_if AND NOT std::get, BECAUSE THIS BUILD HAS NO EXCEPTIONS.
// build_host.py compiles every C++ filter with -fno-exceptions, and std::get on
// the wrong alternative throws std::bad_variant_access, which with that flag
// becomes a call to std::terminate. std::get_if returns a pointer instead and
// cannot fail, so the filter tests the pointer. P04 reached the same place with
// std::visit; both are checked at compile time and neither can throw.
//
// AND IT STILL NEEDS -ffp-contract=off, WHICH THE RUST DOES NOT. gcc at -O2
// fuses a multiply followed by an add, rounding once where two operations round
// twice, and the two routes to a total in this file are exactly that shape. The
// flag is in build_host.py and
// python/tests/test_float_builds_disable_contraction.py requires it, reading
// this header as well as the source since Wednesday 28 October 2026. The Rust
// needs no flag because fusing there is `mul_add` and nothing else.
#ifndef P10_PHASELEDGER_HPP
#define P10_PHASELEDGER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace p10 {

// Three pins, so eight codes, which is chapter 10's protocol exactly: 0 sleep,
// 1 wake, 2 sense, 3 compute, 4 send, 7 fault, and 5 and 6 unused. The unused
// codes are carried rather than refused, because a trace that contains one is a
// finding and the report should be able to show it.
constexpr std::size_t kCodes = 8;

// The same four refusals as the C, in the same order, so the names mean one
// thing in all four implementations.
enum class Error {
    kArgument = 1,  // no samples, or a null pointer in the C's case
    kInterval,      // a sample interval of zero or below
    kFloor,         // a rejection floor of zero samples
    kCode,          // a marker code above seven
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kInterval: return "interval";
    case Error::kFloor:    return "floor";
    case Error::kCode:     return "code";
    }
    return "unknown";
}

struct Ledger {
    std::array<double, kCodes> per_code{};  // charge in coulombs, by code
    double total = 0.0;         // integrated with no reference to markers
    double split = 0.0;         // the sum of the kept runs
    double residual = 0.0;      // |total - split| / |total|, the test itself
    std::uint32_t runs = 0;
    std::uint32_t rejected = 0;
    std::uint32_t rejected_samples = 0;
};

// The answer or the refusal, never both and never half of either.
using Answer = std::variant<Ledger, Error>;

// The C's !(dt_s > 0.0), written out so the comparison is the same one. It
// refuses zero, every negative value and any value that is not a number, and it
// accepts an infinite interval, which no oracle case reaches and which it would
// be a divergence to refuse here alone. The Rust spells the same predicate
// `x.is_nan() || x <= 0.0`, because clippy rejects this shape as
// neg_cmp_op_on_partial_ord.
inline bool not_above_zero(double x)
{
    return !(x > 0.0);
}

// The C writes the magnitude out by choice rather than by need, and the Rust by
// need, since f64::abs lives in std and that crate is no_std. Written out here
// too, so all four implementations compute the residual from the same three
// operations.
inline double magnitude(double v)
{
    return (v < 0.0) ? -v : v;
}

inline Answer phase_ledger(const double *current_a, const std::uint8_t *codes,
                           std::size_t n, double dt_s,
                           std::uint32_t min_samples)
{
    if (current_a == nullptr || codes == nullptr || n == 0u) {
        return Error::kArgument;
    }
    if (not_above_zero(dt_s)) {
        return Error::kInterval;
    }
    if (min_samples == 0u) {
        return Error::kFloor;
    }
    for (std::size_t i = 0u; i < n; ++i) {
        if (static_cast<std::size_t>(codes[i]) >= kCodes) {
            return Error::kCode;
        }
    }

    Ledger out;

    // The total, integrated first and with no reference to the markers at all.
    double total = 0.0;
    for (std::size_t i = 0u; i < n; ++i) {
        total += current_a[i];
    }
    total *= dt_s;

    // The runs, by index rather than by time.
    double split = 0.0;
    std::size_t start = 0u;
    for (std::size_t i = 1u; i <= n; ++i) {
        if (i == n || codes[i] != codes[start]) {
            const std::size_t count = i - start;
            if (count >= static_cast<std::size_t>(min_samples)) {
                double q = 0.0;
                for (std::size_t k = start; k < i; ++k) {
                    q += current_a[k];
                }
                q *= dt_s;
                out.per_code[static_cast<std::size_t>(codes[start])] += q;
                split += q;
                ++out.runs;
            } else {
                ++out.rejected;
                out.rejected_samples += static_cast<std::uint32_t>(count);
            }
            start = i;
        }
    }

    out.total = total;
    out.split = split;
    // A trace that drew nothing has no denominator, so the residual is reported
    // as one rather than as a division, which is what the other three do.
    out.residual = (total != 0.0)
                 ? (magnitude(total - split) / magnitude(total))
                 : 1.0;
    return out;
}

}  // namespace p10

#endif  // P10_PHASELEDGER_HPP
