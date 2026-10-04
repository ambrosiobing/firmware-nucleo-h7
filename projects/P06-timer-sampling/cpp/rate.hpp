// projects/P06-timer-sampling/cpp/rate.hpp: the witness in C++17, header only.
//
// The same arithmetic, in the same order, as `../c/rate.c` and
// `python/firmkit/rate.py`. The order matters and is not a matter of style: a
// sum accumulated differently gives a different double, and the parity test
// compares these numbers.
//
// THE HALF OF P06 THAT CAN BE PROVEN. The three acquisition back ends refuse at
// run time rather than guessing a setting RM0455 governs. The witness needs none
// of that, and it is the piece that decides whether a clean square wave at a
// plausible wrong rate is reported as a pass, which is this project's subject.
//
// ON FLOATING POINT. The four implementations are compared to a relative
// tolerance of 1e-12 rather than bit for bit, and the reason is worth stating
// rather than hiding behind a tolerance. Double arithmetic is deterministic,
// but the contraction of a multiply and an add into one fused instruction is
// not the same across toolchains: gcc may contract where rustc does not, and
// the Cortex-M7's floating point unit has a fused multiply-add where a host
// may not. `-ffp-contract=off` removes the question where it can be removed,
// and the tolerance covers what remains. The pass criteria are 0.1 percent, so
// 1e-12 is nine orders of magnitude tighter than anything the measurement
// claims.
//
// WHAT C++ ADDS HERE. Very little, and that is the honest answer. The witness is
// floating-point arithmetic over an array, which C expresses as well as C++
// does. `std::optional` makes the refusal a different type rather than a
// negative return code, and `std::array` fixes the buffer sizes without a
// macro. There is no `constexpr` self-test, because the arithmetic involves
// `sqrt` on values a compile-time case would have to carry as a literal, and a
// hand-computed expected double would be a second implementation of the thing
// under test rather than a check on it.
#ifndef P06_RATE_HPP
#define P06_RATE_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace p06 {

// The criteria from docs/measurement.md, written before any hardware was
// connected. Changing a number here is a change to the claim, so it is a change
// to that document and to the other three implementations.
inline constexpr double nominal_hz_default = 1000.0;
inline constexpr double rate_tolerance_fraction = 0.001;      // criterion 1
inline constexpr double jitter_sd_max_s = 10e-6;              // criterion 2
inline constexpr double worst_interval_dev_max_s = 50e-6;     // criterion 3
inline constexpr std::size_t max_missing_edges = 0;           // criterion 4

inline constexpr std::size_t max_samples = 65536;
inline constexpr std::size_t max_edges = 4096;

enum class Status {
    Ok,
    TooFewEdges,      // fewer than three; nothing to measure
    NoSwing,          // a flat recording has no edges to find
    TooManyEdges,
    Args,
};

constexpr const char* name(Status s)
{
    switch (s) {
    case Status::Ok:           return "ok";
    case Status::TooFewEdges:  return "too_few_edges";
    case Status::NoSwing:      return "no_swing";
    case Status::TooManyEdges: return "too_many_edges";
    case Status::Args:         return "args";
    }
    return "?";
}

struct Result {
    double sample_rate_hz = 0;
    double duration_s = 0;
    std::size_t edges = 0;
    double nominal_hz = 0;
    double resolution_s = 0;

    double count_rate_hz = 0;    // route one
    double fit_period_s = 0;     // route two
    double fit_rate_hz = 0;
    double fit_rate_se_hz = 0;

    double interval_mean_s = 0;
    double interval_sd_s = 0;
    double interval_worst_s = 0;
    double interval_worst_dev_s = 0;
    std::size_t missing_edges = 0;

    bool routes_agree = false;
    bool rate_within_tolerance = false;
    bool jitter_within_limit = false;
    bool worst_interval_within_limit = false;
    bool no_missing_edges = false;
    bool pass = false;
    bool limited_by_instrument = false;
};

// Rising edge times in seconds, by linear interpolation across the midpoint of
// a Schmitt-style pair of thresholds taken from the observed swing rather than
// assumed to be rail voltages. Interpolation is what gets an edge time below one
// sample period, which criterion 2 depends on.
//
// Returns the number of edges written, or the status that refused.
inline std::optional<std::size_t> rising_edges(const double* samples, std::size_t count,
                                               double fs, double* edges_out,
                                               std::size_t edges_cap, Status& why)
{
    why = Status::Ok;
    if (samples == nullptr || edges_out == nullptr || fs <= 0.0) {
        why = Status::Args;
        return std::nullopt;
    }
    // Sixteen samples is the Python's own floor: fewer is a fragment, not a
    // recording of a square wave.
    if (count < 16) {
        return std::size_t{0};
    }

    double lo_obs = samples[0];
    double hi_obs = samples[0];
    for (std::size_t i = 1; i < count; ++i) {
        if (samples[i] < lo_obs) { lo_obs = samples[i]; }
        if (samples[i] > hi_obs) { hi_obs = samples[i]; }
    }
    const double swing = hi_obs - lo_obs;
    if (swing <= 0.0) {
        why = Status::NoSwing;
        return std::nullopt;
    }

    const double low = lo_obs + 0.3 * swing;
    const double high = lo_obs + 0.7 * swing;
    const double mid = 0.5 * (low + high);

    std::size_t written = 0;
    bool armed = samples[0] < low;   // must go low before a rise counts

    for (std::size_t i = 1; i < count; ++i) {
        const double prev = samples[i - 1];
        const double cur = samples[i];
        if (armed && cur >= high) {
            double frac = 0.0;
            if (cur != prev) {
                frac = (mid - prev) / (cur - prev);
                if (frac < 0.0) { frac = 0.0; }
                if (frac > 1.0) { frac = 1.0; }
            }
            if (written >= edges_cap) {
                why = Status::TooManyEdges;
                return std::nullopt;
            }
            edges_out[written++] = (static_cast<double>(i - 1) + frac) / fs;
            armed = false;
        } else if (cur < low) {
            armed = true;
        }
    }
    return written;
}

// Least squares gradient, intercept and the standard error of the gradient,
// written out so the witness needs nothing beyond the standard library.
struct Fit {
    double slope = 0;
    double intercept = 0;
    double slope_se = 0;
};

inline std::optional<Fit> fit_line(const double* x, const double* y, std::size_t count)
{
    if (x == nullptr || y == nullptr || count < 3) {
        return std::nullopt;
    }
    const double n = static_cast<double>(count);
    double sx = 0.0;
    double sy = 0.0;
    for (std::size_t i = 0; i < count; ++i) { sx += x[i]; }
    for (std::size_t i = 0; i < count; ++i) { sy += y[i]; }
    const double mx = sx / n;
    const double my = sy / n;

    double sxx = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double d = x[i] - mx;
        sxx += d * d;
    }
    if (sxx == 0.0) {
        return std::nullopt;
    }

    double sxy = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sxy += (x[i] - mx) * (y[i] - my);
    }
    const double m = sxy / sxx;
    const double c = my - m * mx;

    double ss = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double r = y[i] - (m * x[i] + c);
        ss += r * r;
    }
    const double s2 = ss / (n - 2.0);

    return Fit{m, c, std::sqrt(s2 / sxx)};
}

// The whole analysis. On a refusal the result still carries what was known
// before it, so a caller can print the edge count.
inline Status analyse(const double* samples, std::size_t count, double fs,
                      double nominal_hz, Result& out)
{
    if (samples == nullptr) {
        return Status::Args;
    }
    static std::array<double, max_edges> edges{};
    static std::array<double, max_edges> index{};

    out = Result{};
    out.sample_rate_hz = fs;
    out.duration_s = (fs > 0.0) ? static_cast<double>(count) / fs : 0.0;
    out.nominal_hz = nominal_hz;
    out.resolution_s = (fs > 0.0) ? 1.0 / fs : std::nan("");

    Status why = Status::Ok;
    const std::optional<std::size_t> found =
        rising_edges(samples, count, fs, edges.data(), edges.size(), why);
    if (!found) {
        return why;
    }
    out.edges = *found;
    if (*found < 3) {
        out.pass = false;
        return Status::TooFewEdges;
    }

    const std::size_t n = *found;

    const double span = edges[n - 1] - edges[0];
    out.count_rate_hz = (span > 0.0) ? static_cast<double>(n - 1) / span : std::nan("");

    for (std::size_t i = 0; i < n; ++i) {
        index[i] = static_cast<double>(i);
    }
    const std::optional<Fit> fit = fit_line(index.data(), edges.data(), n);
    const double period = fit ? fit->slope : std::nan("");
    const double period_se = fit ? fit->slope_se : std::nan("");
    out.fit_period_s = period;
    out.fit_rate_hz = (period != 0.0) ? 1.0 / period : std::nan("");
    out.fit_rate_se_hz = (period != 0.0) ? period_se / (period * period) : std::nan("");

    const std::size_t ivs = n - 1;
    double sum_iv = 0.0;
    for (std::size_t i = 0; i < ivs; ++i) {
        sum_iv += edges[i + 1] - edges[i];
    }
    const double mean_iv = sum_iv / static_cast<double>(ivs);
    out.interval_mean_s = mean_iv;

    double var = 0.0;
    if (ivs > 1) {
        double acc = 0.0;
        for (std::size_t i = 0; i < ivs; ++i) {
            const double d = (edges[i + 1] - edges[i]) - mean_iv;
            acc += d * d;
        }
        var = acc / static_cast<double>(ivs - 1);
    }
    out.interval_sd_s = std::sqrt(var);

    const double nominal_iv = 1.0 / nominal_hz;
    double worst = edges[1] - edges[0];
    double worst_dev = std::fabs(worst - nominal_iv);
    for (std::size_t i = 1; i < ivs; ++i) {
        const double v = edges[i + 1] - edges[i];
        const double dev = std::fabs(v - nominal_iv);
        // Strictly greater, so the first of two equally bad intervals wins,
        // which is what Python's max does.
        if (dev > worst_dev) {
            worst = v;
            worst_dev = dev;
        }
    }
    out.interval_worst_s = worst;
    out.interval_worst_dev_s = worst_dev;

    // A dropped edge shows as an interval near an integer multiple of nominal,
    // a doubled edge as one near a half. Both are missing edges. nearbyint
    // rounds half to even under the default mode, which is what Python's round
    // does, and that agreement is the reason it is used here rather than round.
    std::size_t missing = 0;
    for (std::size_t i = 0; i < ivs; ++i) {
        const double v = edges[i + 1] - edges[i];
        const double k = v / nominal_iv;
        const double nearest = std::nearbyint(k);
        if (nearest >= 2.0 && std::fabs(k - nearest) < 0.25) {
            missing += static_cast<std::size_t>(nearest) - 1;
        } else if (nearest == 0.0 || (k < 0.75 && k > 0.0)) {
            missing += 1;
        }
    }
    out.missing_edges = missing;

    out.routes_agree = !std::isnan(out.fit_rate_hz)
                       && std::fabs(out.count_rate_hz - out.fit_rate_hz)
                              < 0.01 * nominal_hz;

    out.rate_within_tolerance =
        std::fabs(out.fit_rate_hz - nominal_hz) <= rate_tolerance_fraction * nominal_hz;
    out.jitter_within_limit = out.interval_sd_s <= jitter_sd_max_s;
    out.worst_interval_within_limit = out.interval_worst_dev_s <= worst_interval_dev_max_s;
    out.no_missing_edges = out.missing_edges <= max_missing_edges;

    out.pass = out.rate_within_tolerance && out.jitter_within_limit
               && out.worst_interval_within_limit && out.no_missing_edges
               && out.routes_agree;

    out.limited_by_instrument = out.interval_sd_s < out.resolution_s / 2.0;
    return Status::Ok;
}

}  // namespace p06

#endif  // P06_RATE_HPP
