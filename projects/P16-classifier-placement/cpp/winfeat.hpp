// projects/P16-classifier-placement/cpp/winfeat.hpp: the window features and
// the bounded tree in C++17.
//
// The same arithmetic, in the same order, as `../c/winfeat.c`,
// `../python/winfeat.py` and `../rust/src/lib.rs`. One window is 128 samples of
// three axes, which is chapter 16 step 3's definition, and the features are Q15.
//
// WHAT C++ ADDS HERE, AND THIS TIME IT IS SOMETHING THE OTHER THREE CANNOT DO.
// The honest answer varies by project in this volume and is worth keeping
// accurate. P18's cascade is one function template where the C needs two forty
// line copies. P17 is four small functions over fixed-width integers and C++
// adds almost nothing. Here it adds a FOURTH INDEPENDENT ALGORITHM for the one
// quantity in this project where two implementations have real room to
// disagree, and it is the algorithm the C header refuses by name.
//
// THE C HEADER'S REFUSAL, TESTED RATHER THAN REPEATED. `../c/winfeat.c` writes
// its integer square root as a bit-by-bit descent and says why: "A float sqrt
// followed by a cast would agree almost always, and almost always is what an
// exact gate exists to refuse." That is correct about sqrt-and-cast and it is
// NOT an argument against floating point as such. A sqrt followed by a
// CORRECTION is exact for every input, and that is what this file does:
//
//     r = (uint32_t) std::sqrt((double) v);   // close, and not trusted
//     while (r * r > v) { --r; }              // too high by one or more
//     while ((r + 1) * (r + 1) <= v) { ++r; } // too low by one or more
//
// So the four implementations now reach the same floored quantity four ways: a
// bit-by-bit descent, `math.isqrt`, `u64::isqrt`, and a corrected sqrt. That is
// the most independence this quantity can be given, and criterion 1 is worth
// running in proportion to it. It already paid once: the third implementation
// found a corner where the first two disagreed, which is the saturation below.
//
// AND THE MEASUREMENT SAYS SOMETHING SHARPER THAN THE C HEADER DOES, WHICH IS
// THIS FILE'S OWN FINDING. The correction loops never fire for any input this
// project can produce. The mean of the squares cannot exceed 1073741824, which
// is two to the thirtieth, and across 298267 values covering every k squared
// less one, k squared and k squared plus one for every k up to 32768, the whole
// range's endpoints and 200000 random draws, the uncorrected sqrt and cast was
// already exact every time.
//
// SO WITHIN THIS PROJECT'S RANGE THE ROUTE THE C HEADER REFUSES WOULD HAVE BEEN
// CORRECT, AND THE REFUSAL IS STILL RIGHT FOR A DIFFERENT REASON. The place
// where sqrt and cast fails has a shape: v equal to k squared less one, where
// the true root is a hair below a whole number and the double rounds up to it.
// That needs the hair to be narrower than half a unit of least precision, which
// first happens at k equal to two to the twenty-sixth. The smallest failing
// input is 4503599761588224, which is 67108865 squared less one, where the cast
// gives 67108865 and the answer is 67108864. This project's roots stop at
// 32768, which is two to the fifteenth and eleven binary orders of magnitude
// below that threshold.
//
// The C header's sentence is therefore correct as a rule about a function typed
// uint64_t and NOT correct as a statement about this window's arithmetic. Both
// halves are worth having: the loops stay, because the type says uint64_t and
// the next caller may not be this project, and a test asserts both that they
// never fire across the project's range and that they do fire at 67108865
// squared less one, so they are not dead code anywhere. A third case covers the
// clamp, which fires for the top two values a uint64_t can hold.
//
// WHAT ELSE C++ GIVES, WHICH IS THE USUAL SMALL LIST. `std::optional<Error>`
// makes a refusal a different type rather than a negative return code, and
// `std::array` fixes the window without a macro.
//
// THE ROUNDING IS PINNED AND THE SATURATION IS THE PROJECT'S FINDING. Step 3
// fixes the window, the axes, the feature list and the numeric format and says
// nothing about rounding, so the mean and the root mean square both truncate
// toward negative infinity here as they do in the other three. Both the root
// mean square and peak to peak saturate at 32767: on a window of negative full
// scale the root is exactly 32768, one above what Q15 holds, and until Tuesday
// 27 October 2026 the C wrapped it and the Python returned a value Q15 cannot
// hold. The band energies of step 3 are absent for the reason the C header
// gives.
#ifndef P16_WINFEAT_HPP
#define P16_WINFEAT_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace p16 {

constexpr std::size_t kWinN = 128;
constexpr std::size_t kAxes = 3;
constexpr std::size_t kGroups = 3;
constexpr std::size_t kCount = kGroups * kAxes;
constexpr std::size_t kTreeMax = 64;

// The same six refusals as the C, in the same order, so a name means the same
// thing in all four implementations.
enum class Error {
    kArgument = 1,
    kShape,
    kTree,
    kNode,
    kFeature,
    kDepth,
};

inline const char *name(Error e)
{
    switch (e) {
    case Error::kArgument: return "argument";
    case Error::kShape:    return "shape";
    case Error::kTree:     return "tree";
    case Error::kNode:     return "node";
    case Error::kFeature:  return "feature";
    case Error::kDepth:    return "depth";
    }
    return "unknown";
}

struct Features {
    std::array<std::int16_t, kAxes> mean;
    std::array<std::int16_t, kAxes> rms;
    std::array<std::int16_t, kAxes> p2p;
};

// A node is a feature index, a threshold and two children. A leaf points at
// itself and carries its class in the feature field.
struct Node {
    std::uint8_t feature;
    bool leaf;
    std::int16_t threshold;
    std::uint8_t left;
    std::uint8_t right;
};

// The fourth route to the floored integer square root, and the one the C
// header refuses in its uncorrected form. Exact for every input, by the two
// loops rather than by trusting the sqrt.
inline std::uint32_t isqrt_floor(std::uint64_t v)
{
    if (v == 0u) {
        return 0u;
    }
    std::uint64_t r = static_cast<std::uint64_t>(std::sqrt(static_cast<double>(v)));
    // The floored root of the largest uint64_t is 0xFFFFFFFF, so clamping here
    // costs no correct answer and keeps every product below within range: with
    // r at or below 0xFFFFFFFF, r * r cannot overflow, and the second loop
    // computes (r + 1) squared only while r is strictly below that, so r + 1
    // cannot either. Without the clamp, sqrt of the largest input rounds up to
    // 2 to the 32nd and (r + 1) squared wraps to zero, which compares at or
    // below any v and walks the counter upward for ever.
    if (r > 0xFFFFFFFFull) {
        r = 0xFFFFFFFFull;
    }
    while (r > 0u && r * r > v) {
        --r;
    }
    while (r < 0xFFFFFFFFull && (r + 1u) * (r + 1u) <= v) {
        ++r;
    }
    return static_cast<std::uint32_t>(r);
}

// Features for one window, interleaved by axis, which is the order the sensor's
// buffer delivers.
inline std::optional<Error> window(const std::int16_t *samples, std::size_t n,
                                   std::size_t axes, Features &out)
{
    if (samples == nullptr) {
        return Error::kArgument;
    }
    if (n == 0u) {
        return Error::kArgument;
    }
    // Refused rather than scaled: a feature over a different window length is a
    // different feature, and step 3 fixes the length.
    if (n != kWinN || axes != kAxes) {
        return Error::kShape;
    }

    for (std::size_t a = 0u; a < kAxes; ++a) {
        std::int32_t sum = 0;
        std::uint64_t squares = 0u;
        std::int32_t lowest = 32767;
        std::int32_t highest = -32768;

        for (std::size_t i = 0u; i < kWinN; ++i) {
            const std::int32_t v = samples[i * kAxes + a];
            sum += v;
            squares += static_cast<std::uint64_t>(
                static_cast<std::int64_t>(v) * static_cast<std::int64_t>(v));
            if (v < lowest) {
                lowest = v;
            }
            if (v > highest) {
                highest = v;
            }
        }

        // 128 is two to the seventh, so the mean is a division by a power of two
        // with a stated direction. Written as a division and a correction rather
        // than a shift, because a right shift of a negative signed value is
        // implementation defined and the direction is the thing being pinned.
        std::int32_t q = sum / 128;
        if (sum < 0 && (sum % 128) != 0) {
            q -= 1;
        }
        out.mean[a] = static_cast<std::int16_t>(q);

        // The mean of the squares, floored, then its root, floored, then
        // saturated. Q15 stops one short of the 32768 a window of negative full
        // scale produces.
        const std::uint32_t root = isqrt_floor(squares >> 7u);
        out.rms[a] = (root > 32767u) ? static_cast<std::int16_t>(32767)
                                     : static_cast<std::int16_t>(root);

        // Peak to peak can reach 65535, which Q15 cannot hold, so it saturates
        // rather than wrapping to a number that would read as a quiet window.
        const std::int32_t span = highest - lowest;
        out.p2p[a] = (span > 32767) ? static_cast<std::int16_t>(32767)
                                    : static_cast<std::int16_t>(span);
    }
    return std::nullopt;
}

// The nine features flat, in the order a tree's feature indices refer to.
inline std::array<std::int16_t, kCount> vector(const Features &f)
{
    std::array<std::int16_t, kCount> out{};
    for (std::size_t a = 0u; a < kAxes; ++a) {
        out[a] = f.mean[a];
        out[kAxes + a] = f.rms[a];
        out[2u * kAxes + a] = f.p2p[a];
    }
    return out;
}

// Walk the tree and return a class. depth_max is the bound step 5 relies on for
// its latency claim, and exceeding it is a refusal rather than a default.
inline std::optional<Error> classify(const Node *nodes, std::size_t count,
                                     std::size_t depth_max,
                                     const std::int16_t *features,
                                     std::size_t nfeat, std::uint8_t &out)
{
    if (nodes == nullptr || features == nullptr) {
        return Error::kArgument;
    }
    if (count == 0u || count > kTreeMax) {
        return Error::kTree;
    }
    if (depth_max == 0u) {
        return Error::kDepth;
    }
    if (nfeat == 0u) {
        return Error::kFeature;
    }

    std::size_t at = 0u;
    for (std::size_t step = 0u; step <= depth_max; ++step) {
        if (at >= count) {
            return Error::kNode;
        }
        const Node &node = nodes[at];
        if (node.leaf) {
            out = node.feature;
            return std::nullopt;
        }
        if (static_cast<std::size_t>(node.feature) >= nfeat) {
            return Error::kFeature;
        }
        // At or below the threshold goes left, stated because the boundary is
        // where two implementations of one tree disagree and the gate compares
        // classes.
        at = (features[node.feature] <= node.threshold)
            ? static_cast<std::size_t>(node.left)
            : static_cast<std::size_t>(node.right);
    }
    return Error::kDepth;
}

}  // namespace p16

#endif  // P16_WINFEAT_HPP
