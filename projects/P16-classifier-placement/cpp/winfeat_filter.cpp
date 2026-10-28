// P16's C++ filter: one request per line on stdin, one answer per line on
// stdout.
//
// It speaks the same two verbs as ../rust/src/main.rs, so one parity test
// drives both with the same code. Each verb takes its counts before its data:
//
//     F <n> <axes> <n*axes samples>
//         -> mean=a,b,c rms=a,b,c p2p=a,b,c vector=v0,..,v8
//     C <nodes> <depth_max> <nfeat> <feature leaf threshold left right, per
//       node> <nfeat features>
//         -> class=k
//
// A refusal is REFUSED followed by one of the six names the C's six error codes
// carry.
//
// Every value is read as a token and converted with strtol rather than with the
// stream extractor, and a token with anything left in it is rejected. That is a
// correction P18's filter was given on Monday 26 October 2026, for a reason
// that does not arise here and is kept anyway: `in >> v` on a double cannot
// parse "inf" or "nan" in libstdc++, so a protocol built on the extractor
// cannot carry a value the implementation is meant to reject. This project's
// values are all integers, so the trap is not reachable, and the habit costs
// nothing.
//
// A request with anything left over is refused, which caught a real defect in
// P18's test on the same day: ignoring a remainder lets a caller send one thing
// and be answered about another.
//
// One window is 384 integers, so an F request is 386 tokens. It reads whole
// lines with getline rather than into a fixed buffer, after P08's filter split
// a twenty-one-thousand-character request into several and answered twenty
// requests with twenty five answers.

#include "winfeat.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool read_long(std::istringstream &in, long lowest, long highest, long &out)
{
    std::string token;
    if (!(in >> token)) {
        return false;
    }
    char *end = nullptr;
    const long v = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0') {
        return false;
    }
    if (v < lowest || v > highest) {
        return false;
    }
    out = v;
    return true;
}

bool read_i16(std::istringstream &in, std::int16_t &out)
{
    long v = 0;
    if (!read_long(in, -32768, 32767, v)) {
        return false;
    }
    out = static_cast<std::int16_t>(v);
    return true;
}

bool read_u8(std::istringstream &in, std::uint8_t &out)
{
    long v = 0;
    if (!read_long(in, 0, 255, v)) {
        return false;
    }
    out = static_cast<std::uint8_t>(v);
    return true;
}

bool read_size(std::istringstream &in, std::size_t &out)
{
    long v = 0;
    // An upper bound well above anything the oracle sends, so a malformed
    // count cannot ask for an allocation measured in gigabytes.
    if (!read_long(in, 0, 1000000, v)) {
        return false;
    }
    out = static_cast<std::size_t>(v);
    return true;
}

void refused(p16::Error e)
{
    std::printf("REFUSED %s\n", p16::name(e));
}

std::string joined(const std::int16_t *values, std::size_t n)
{
    std::string out;
    for (std::size_t i = 0u; i < n; ++i) {
        if (i > 0u) {
            out += ",";
        }
        out += std::to_string(values[i]);
    }
    return out;
}

int do_features(std::istringstream &in)
{
    std::size_t n = 0u;
    std::size_t axes = 0u;
    if (!read_size(in, n) || !read_size(in, axes)) {
        return -1;
    }
    std::vector<std::int16_t> samples;
    samples.reserve(n * axes);
    for (std::size_t i = 0u; i < n * axes; ++i) {
        std::int16_t v = 0;
        if (!read_i16(in, v)) {
            return -1;
        }
        samples.push_back(v);
    }

    // The C refuses a null pointer and a window of no samples with different
    // codes, and an empty request is the second of those, so the pointer is
    // only ever null when there is nothing to point at. A one element buffer
    // keeps that distinction the C's rather than this filter's.
    std::int16_t none = 0;
    const std::int16_t *data = samples.empty() ? &none : samples.data();

    p16::Features f{};
    if (const auto bad = p16::window(data, n, axes, f)) {
        refused(*bad);
        return 0;
    }
    const auto flat = p16::vector(f);
    std::printf("mean=%s rms=%s p2p=%s vector=%s\n",
                joined(f.mean.data(), p16::kAxes).c_str(),
                joined(f.rms.data(), p16::kAxes).c_str(),
                joined(f.p2p.data(), p16::kAxes).c_str(),
                joined(flat.data(), p16::kCount).c_str());
    return 0;
}

int do_classify(std::istringstream &in)
{
    std::size_t count = 0u;
    std::size_t depth_max = 0u;
    std::size_t nfeat = 0u;
    if (!read_size(in, count) || !read_size(in, depth_max)
        || !read_size(in, nfeat)) {
        return -1;
    }

    std::vector<p16::Node> nodes;
    nodes.reserve(count);
    for (std::size_t i = 0u; i < count; ++i) {
        std::uint8_t feature = 0u;
        std::uint8_t leaf = 0u;
        std::int16_t threshold = 0;
        std::uint8_t left = 0u;
        std::uint8_t right = 0u;
        if (!read_u8(in, feature) || !read_u8(in, leaf)
            || !read_i16(in, threshold) || !read_u8(in, left)
            || !read_u8(in, right)) {
            return -1;
        }
        nodes.push_back(p16::Node{feature, leaf != 0u, threshold, left, right});
    }

    std::vector<std::int16_t> features;
    features.reserve(nfeat);
    for (std::size_t i = 0u; i < nfeat; ++i) {
        std::int16_t v = 0;
        if (!read_i16(in, v)) {
            return -1;
        }
        features.push_back(v);
    }

    p16::Node no_node{};
    std::int16_t no_feature = 0;
    const p16::Node *tree = nodes.empty() ? &no_node : nodes.data();
    const std::int16_t *feats =
        features.empty() ? &no_feature : features.data();

    std::uint8_t klass = 255u;
    if (const auto bad = p16::classify(tree, count, depth_max, feats, nfeat,
                                       klass)) {
        refused(*bad);
        return 0;
    }
    std::printf("class=%u\n", static_cast<unsigned>(klass));
    return 0;
}

}  // namespace

int main()
{
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream in(line);
        std::string verb;
        if (!(in >> verb)) {
            std::printf("BAD_REQUEST\n");
            return 1;
        }
        int rc = 0;
        if (verb == "F") {
            rc = do_features(in);
        } else if (verb == "C") {
            rc = do_classify(in);
        } else {
            std::printf("BAD_VERB %s\n", verb.c_str());
            return 1;
        }
        if (rc != 0) {
            std::printf("BAD_REQUEST\n");
            return 1;
        }
        std::string leftover;
        if (in >> leftover) {
            std::printf("BAD_REQUEST trailing %s\n", leftover.c_str());
            return 1;
        }
    }
    return 0;
}
