// projects/P18-transforms-filters/cpp/dspaccept.hpp: the cascade in C++17.
//
// The same arithmetic, in the same order, as `../c/dspaccept.c` and
// `../python/dspaccept.py`. The order matters and is not a matter of style: a
// sum accumulated differently gives a different double, and the parity test
// compares these numbers bit for bit on every case the oracle marks exact.
//
// WHAT C++ ADDS HERE, and for once the answer is something. The C carries TWO
// cascade functions, `biquad_df2t_f64` and `biquad_df2t_f32`, forty lines each
// and identical but for the type of five locals. That duplication is not
// stylistic: it is two places a change has to be made and one place it can be
// forgotten, in the file whose whole subject is whether two numeric formats
// agree. Here it is one function template instantiated at `double` and at
// `float`, so the two paths cannot drift apart. The chapter's subject is the
// comparison between formats, and a language that expresses the comparison in
// one body is saying something about the problem rather than about itself.
//
// ON FLOATING POINT. The exact cases are compared bit for bit, which is
// available only because their coefficients are powers of two and their inputs
// are Q15 integers, so no intermediate needs rounding and a fused multiply-add
// cannot change an answer it never rounds. The design case is compared to a
// relative 1e-12 for the reason P06's header gives at length: contraction is
// not the same across gcc, g++ and rustc. `-ffp-contract=off` removes the
// question where it can be removed and the tolerance covers what remains.
//
// WHAT IS NOT HERE is what is not in the C either: the real transform and its
// packed output, the cycle counter, and the eight-case placement grid. Those
// need the part.
#ifndef P18_DSPACCEPT_HPP
#define P18_DSPACCEPT_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace p18 {

constexpr std::size_t kMaxStage = 8;
constexpr std::size_t kCoeffPerStage = 5;
constexpr std::int32_t kMaxPostShift = 7;

// The same six refusals as the C, in the same order, so a negative code means
// the same thing in both. The filter prints the name and the test maps it.
enum class Error {
    kArgument = 1,
    kStages,
    kShift,
    kRange,
    kFinite,
    kReference,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument:  return "argument";
    case Error::kStages:    return "stages";
    case Error::kShift:     return "shift";
    case Error::kRange:     return "range";
    case Error::kFinite:    return "finite";
    case Error::kReference: return "reference";
    }
    return "unknown";
}

struct Accept {
    double rel_rms;
    double snr_db;
    bool exact;
};

inline std::optional<Error> check_stages(std::size_t nstage,
                                         std::size_t coeff_count)
{
    if (nstage == 0 || nstage > kMaxStage) {
        return Error::kStages;
    }
    if (coeff_count < nstage * kCoeffPerStage) {
        return Error::kStages;
    }
    return std::nullopt;
}

// ONE cascade for both formats. T is double or float; nothing else is intended
// and nothing else is instantiated.
template <typename T>
std::optional<Error> biquad_df2t(const std::vector<T> &coeffs,
                                 std::size_t nstage,
                                 const std::vector<T> &in,
                                 std::vector<T> &out)
{
    if (in.empty()) {
        return Error::kArgument;
    }
    if (const auto bad = check_stages(nstage, coeffs.size())) {
        return bad;
    }
    for (std::size_t c = 0; c < nstage * kCoeffPerStage; ++c) {
        if (!std::isfinite(coeffs[c])) {
            return Error::kFinite;
        }
    }

    out = in;
    for (std::size_t s = 0; s < nstage; ++s) {
        const T b0 = coeffs[s * kCoeffPerStage + 0];
        const T b1 = coeffs[s * kCoeffPerStage + 1];
        const T b2 = coeffs[s * kCoeffPerStage + 2];
        const T a1 = coeffs[s * kCoeffPerStage + 3];
        const T a2 = coeffs[s * kCoeffPerStage + 4];
        T d1 = static_cast<T>(0);
        T d2 = static_cast<T>(0);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const T x = out[i];
            const T y = b0 * x + d1;
            d1 = b1 * x + a1 * y + d2;
            d2 = b2 * x + a2 * y;
            out[i] = y;
        }
    }
    return std::nullopt;
}

inline double round_half_away_from_zero(double v)
{
    return (v < 0.0) ? -std::floor(-v + 0.5) : std::floor(v + 0.5);
}

// 32768 / 2^shift, built by halving so it is exact in binary at every shift.
inline double q15_scale(std::int32_t post_shift)
{
    double scale = 32768.0;
    for (std::int32_t i = 0; i < post_shift; ++i) {
        scale *= 0.5;
    }
    return scale;
}

inline std::optional<Error> q15_coeffs(const std::vector<double> &coeffs,
                                       std::size_t nstage,
                                       std::int32_t post_shift,
                                       std::vector<std::int16_t> &out)
{
    if (const auto bad = check_stages(nstage, coeffs.size())) {
        return bad;
    }
    if (post_shift < 0 || post_shift > kMaxPostShift) {
        return Error::kShift;
    }
    const std::size_t total = nstage * kCoeffPerStage;
    const double scale = q15_scale(post_shift);
    for (std::size_t c = 0; c < total; ++c) {
        if (!std::isfinite(coeffs[c])) {
            return Error::kFinite;
        }
    }
    // Checked in full before anything is written, so a refusal leaves the
    // caller's vector untouched rather than half converted.
    for (std::size_t c = 0; c < total; ++c) {
        const double v = round_half_away_from_zero(coeffs[c] * scale);
        if (v < -32768.0 || v > 32767.0) {
            return Error::kRange;
        }
    }
    out.clear();
    out.reserve(total);
    for (std::size_t c = 0; c < total; ++c) {
        out.push_back(static_cast<std::int16_t>(
            round_half_away_from_zero(coeffs[c] * scale)));
    }
    return std::nullopt;
}

inline std::optional<Error> q15_post_shift(const std::vector<double> &coeffs,
                                           std::size_t nstage,
                                           std::int32_t &out)
{
    if (const auto bad = check_stages(nstage, coeffs.size())) {
        return bad;
    }
    const std::size_t total = nstage * kCoeffPerStage;
    for (std::size_t c = 0; c < total; ++c) {
        if (!std::isfinite(coeffs[c])) {
            return Error::kFinite;
        }
    }
    for (std::int32_t shift = 0; shift <= kMaxPostShift; ++shift) {
        const double scale = q15_scale(shift);
        bool fits = true;
        for (std::size_t c = 0; c < total; ++c) {
            const double v = round_half_away_from_zero(coeffs[c] * scale);
            if (v < -32768.0 || v > 32767.0) {
                fits = false;
                break;
            }
        }
        if (fits) {
            out = shift;
            return std::nullopt;
        }
    }
    return Error::kRange;
}

// What an arithmetic right shift of `bits` places does, as the floor division
// it is, so the sign behaviour is in the source rather than in the compiler's
// choice. C++20 defines the shift; C++17 does not, and this file is C++17.
inline std::int64_t floor_shift(std::int64_t v, std::uint32_t bits)
{
    const std::int64_t d = static_cast<std::int64_t>(1) << bits;
    std::int64_t q = v / d;
    if (v < 0 && (v % d) != 0) {
        q -= 1;
    }
    return q;
}

inline std::optional<Error> biquad_df1_q15(
    const std::vector<std::int16_t> &coeffs, std::size_t nstage,
    std::int32_t post_shift, const std::vector<std::int16_t> &in,
    std::vector<std::int16_t> &out, std::uint32_t &saturations)
{
    if (in.empty()) {
        return Error::kArgument;
    }
    if (const auto bad = check_stages(nstage, coeffs.size())) {
        return bad;
    }
    if (post_shift < 0 || post_shift > kMaxPostShift) {
        return Error::kShift;
    }

    saturations = 0;
    out = in;
    const std::uint32_t bits = static_cast<std::uint32_t>(15 - post_shift);

    for (std::size_t s = 0; s < nstage; ++s) {
        const std::int64_t b0 = coeffs[s * kCoeffPerStage + 0];
        const std::int64_t b1 = coeffs[s * kCoeffPerStage + 1];
        const std::int64_t b2 = coeffs[s * kCoeffPerStage + 2];
        const std::int64_t a1 = coeffs[s * kCoeffPerStage + 3];
        const std::int64_t a2 = coeffs[s * kCoeffPerStage + 4];
        std::int64_t x1 = 0;
        std::int64_t x2 = 0;
        std::int64_t y1 = 0;
        std::int64_t y2 = 0;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const std::int64_t x = out[i];
            const std::int64_t acc =
                b0 * x + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2;
            std::int64_t y = floor_shift(acc, bits);
            if (y > 32767) {
                y = 32767;
                ++saturations;
            } else if (y < -32768) {
                y = -32768;
                ++saturations;
            }
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            out[i] = static_cast<std::int16_t>(y);
        }
    }
    return std::nullopt;
}

// Both lengths are arguments for the reason the C header gives: comparing two
// blocks of different lengths against the shorter of them returns a plausible
// number rather than an error, which is the failure chapter 18 opens by naming.
// Here the lengths come with the vectors, and the mismatch is still refused
// rather than trusted, because a caller that passes two different blocks has
// made the same mistake whatever the type system knows.
inline std::optional<Error> accept_metrics(const std::vector<double> &got,
                                           const std::vector<double> &ref,
                                           Accept &out)
{
    if (got.empty() || got.size() != ref.size()) {
        return Error::kArgument;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (!std::isfinite(got[i]) || !std::isfinite(ref[i])) {
            return Error::kFinite;
        }
    }

    // The means, in order, because the C adds in order and these numbers are
    // compared against it.
    double mean_err2 = 0.0;
    double mean_ref2 = 0.0;
    for (std::size_t i = 0; i < got.size(); ++i) {
        const double e = got[i] - ref[i];
        mean_err2 += e * e;
        mean_ref2 += ref[i] * ref[i];
    }
    mean_err2 /= static_cast<double>(got.size());
    mean_ref2 /= static_cast<double>(got.size());

    if (mean_ref2 == 0.0) {
        return Error::kReference;
    }
    if (mean_err2 == 0.0) {
        out.rel_rms = 0.0;
        out.snr_db = INFINITY;
        out.exact = true;
        return std::nullopt;
    }
    out.rel_rms = std::sqrt(mean_err2) / std::sqrt(mean_ref2);
    out.snr_db = 10.0 * std::log10(mean_ref2 / mean_err2);
    out.exact = false;
    return std::nullopt;
}

}  // namespace p18

#endif  // P18_DSPACCEPT_HPP
