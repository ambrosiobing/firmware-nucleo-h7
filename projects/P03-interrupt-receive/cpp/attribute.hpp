// projects/P03-interrupt-receive/cpp/attribute.hpp: the attribution in C++17, header only.
//
// The same arithmetic and the same verdict order as `../c/attribute.c`, written
// independently. The half of P03 that can be proven: the receive path needs a
// peripheral and refuses until RM0455 is read, while this is arithmetic over
// counters a run recorded, and it is the half where a wrong answer publishes a
// wrong conclusion rather than merely failing to work.
//
// THE ORDER OF THE VERDICTS IS LOAD BEARING, in all four languages. BRIDGE is
// tested first, so a step that lost bytes before the peripheral saw them is
// never also reported as a target finding. A run that measured the bridge says
// nothing about the target, and a row claiming both would be the wrong
// conclusion wearing the right shape.
//
// WHAT C++ ADDS HERE. One thing, and it is the same thing P05 got: the whole
// attribution is `constexpr`, so a case can be asserted at compile time. Two
// are, at the bottom of this file: the clean step and the bridge case. A
// translation unit that includes this header does not compile if either is
// wrong, which is a stronger statement than a test that must be run.
//
// The refusal is a `std::optional` rather than a negative return code, which
// makes the two outcomes different types instead of different ranges of one
// integer. That matters more here than it looks: the C returns
// `ATTR_ERR_OVERACCOUNTED` as -1 and a caller that forgot to check would read a
// row of uninitialised fields, while this one cannot be read without being
// unwrapped.
#ifndef P03_ATTRIBUTE_HPP
#define P03_ATTRIBUTE_HPP

#include <cstdint>
#include <optional>

namespace p03 {

// One byte in a million, as a reciprocal so the comparison needs no floating
// point. The Python twin computes the same tolerance as `int(sent * 1e-6)`, and
// the two were checked for divergence rather than assumed equal: `1e-6` is
// slightly below one millionth, so the product is slightly below the true
// quotient, but the multiply rounds back onto the exact integer because the
// relative error is around 2e-17 while the spacing of doubles near the largest
// possible quotient, about 4295, is around 1e-12. They agree for every `sent`
// in the 32-bit range, which was swept rather than argued. The integer form is
// kept here because it is what a target would compute, not because the Python
// is wrong.
inline constexpr std::uint32_t bridge_tolerance_reciprocal = 1000000;

enum class Verdict : std::uint8_t {
    Pass = 0,        // the target lost nothing
    Bridge = 1,      // bytes went missing before the peripheral saw them
    Latency = 2,     // overruns only: the handler was too late
    Throughput = 3,  // drops only: the consumer could not keep up
    Both = 4,        // both kinds, which are still not summed
};

constexpr const char* name(Verdict v)
{
    switch (v) {
    case Verdict::Pass:       return "PASS";
    case Verdict::Bridge:     return "BRIDGE";
    case Verdict::Latency:    return "LATENCY";
    case Verdict::Throughput: return "THROUGHPUT";
    case Verdict::Both:       return "BOTH";
    }
    return "?";
}

// What a run recorded for one step.
struct Step {
    std::uint32_t rate = 0;
    std::uint32_t sent = 0;
    std::uint32_t accepted = 0;
    std::uint32_t overruns = 0;
    std::uint32_t dropped = 0;
};

struct Row {
    std::uint32_t rate = 0;
    std::uint32_t sent = 0;
    std::uint32_t delivered = 0;
    std::uint32_t overrun = 0;
    std::uint32_t dropped = 0;
    std::uint32_t bridge_lost = 0;
    std::uint32_t target_lost = 0;
    std::uint32_t reached = 0;
    Verdict verdict = Verdict::Pass;

    constexpr bool operator==(const Row& o) const
    {
        return rate == o.rate && sent == o.sent && delivered == o.delivered
               && overrun == o.overrun && dropped == o.dropped
               && bridge_lost == o.bridge_lost && target_lost == o.target_lost
               && reached == o.reached && verdict == o.verdict;
    }
};

// Attribute one step, or nothing when the target accounted for more bytes than
// the host sent. That is a defect in the measurement rather than a finding
// about the link: a counter that was not reset between steps, or a wrong host
// count. Reporting a negative bridge loss, or clamping it to zero, would turn
// the defect into a plausible-looking row.
constexpr std::optional<Row> attribute(const Step& step)
{
    const std::uint32_t sent = step.sent;
    const std::uint32_t delivered = step.accepted;
    const std::uint32_t overrun = step.overruns;
    const std::uint32_t dropped = step.dropped;

    // Summed in 64 bits, as the C does and for the same reason: three 32-bit
    // counters can exceed a 32-bit sum between them, and a wrapped sum would
    // make `reached` smaller than it is and turn an over-accounted step into a
    // plausible bridge loss.
    const std::uint64_t reached =
        static_cast<std::uint64_t>(delivered) + overrun + dropped;

    if (reached > static_cast<std::uint64_t>(sent)) {
        return std::nullopt;
    }

    const std::uint32_t bridge_lost =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(sent) - reached);
    const std::uint32_t target_lost = overrun + dropped;

    std::uint32_t tolerance = sent / bridge_tolerance_reciprocal;
    if (tolerance < 1) {
        tolerance = 1;   // a short run is not judged by a tolerance of zero
    }

    Verdict verdict{};
    if (bridge_lost > tolerance) {
        verdict = Verdict::Bridge;
    } else if (target_lost == 0) {
        verdict = Verdict::Pass;
    } else if (overrun > 0 && dropped == 0) {
        verdict = Verdict::Latency;
    } else if (dropped > 0 && overrun == 0) {
        verdict = Verdict::Throughput;
    } else {
        verdict = Verdict::Both;
    }

    Row row{};
    row.rate = step.rate;
    row.sent = sent;
    row.delivered = delivered;
    row.overrun = overrun;
    row.dropped = dropped;
    row.bridge_lost = bridge_lost;
    row.target_lost = target_lost;
    row.reached = static_cast<std::uint32_t>(reached);
    row.verdict = verdict;
    return row;
}

// The index of the lowest-rate row where the target itself lost a byte, or
// nothing when none did.
//
// BRIDGE rows are skipped rather than counted as a target failure, because that
// is exactly the confusion this file exists to prevent. A ramp whose every row
// is BRIDGE has found nothing about the target, and returning nothing says so
// rather than naming the lowest bridge failure.
constexpr std::optional<std::size_t> first_loss(const Row* rows, std::size_t count)
{
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < count; ++i) {
        const Verdict v = rows[i].verdict;
        if (v != Verdict::Latency && v != Verdict::Throughput && v != Verdict::Both) {
            continue;
        }
        if (!best || rows[i].rate < rows[*best].rate) {
            best = i;
        }
    }
    return best;
}

// Two cases asserted by the compiler, which is what `constexpr` buys over the
// C. The clean step and the bridge case: the first is the shape everything else
// is measured against, the second is the one the whole file exists for.
namespace selftest {
inline constexpr Step clean{115200, 1000000, 1000000, 0, 0};
inline constexpr Step bridge{921600, 1000000, 900000, 0, 0};

static_assert(attribute(clean)->verdict == Verdict::Pass,
              "a step that lost nothing must be a pass");
static_assert(attribute(clean)->bridge_lost == 0, "nothing was lost in the bridge");
static_assert(attribute(bridge)->verdict == Verdict::Bridge,
              "a tenth of the traffic missing before the peripheral is the bridge");
static_assert(attribute(bridge)->bridge_lost == 100000, "the bridge lost a tenth");
static_assert(attribute(bridge)->target_lost == 0,
              "the target lost nothing and must not be blamed for the bridge");
}  // namespace selftest

}  // namespace p03

#endif  // P03_ATTRIBUTE_HPP
