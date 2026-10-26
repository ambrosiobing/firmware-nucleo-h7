// P18's filter: one request per line on stdin, one answer per line on stdout.
//
// Six verbs, and every one takes its counts before its data so the parser never
// has to guess where a list ends:
//
//     D <nstage> <m>          5*nstage coefficients, then m samples  (double)
//     S <nstage> <m>          the same, in single precision
//     P <nstage>              5*nstage coefficients: the smallest post shift
//     Q <nstage> <shift>      5*nstage coefficients: the Q15 conversion
//     R <nstage> <shift> <m>  5*nstage Q15 coefficients, then m Q15 samples
//     A <n>                   n got values, then n reference values
//
// Doubles are printed with %.17g, which round-trips a double exactly, so the
// parity test can compare bit for bit on the cases the oracle marks exact. A
// single-precision result is promoted before printing, which is exact: every
// float has an exact double.
//
// IT READS WHOLE LINES WITH getline RATHER THAN INTO A FIXED BUFFER, and that is
// a lesson rather than a preference. P08's filter used char[4096], met a request
// of twenty one thousand characters on Saturday 3 October 2026, and answered
// twenty requests with twenty five answers: it had split one long line into
// several and each fragment parsed into something. A design-case request here
// carries 1024 samples and is far longer than that buffer.
//
// A malformed request is refused loudly on stdout and the process exits
// non-zero. It never guesses and it never silently skips a line, because a
// skipped line misaligns every answer after it.

#include "dspaccept.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Each value is read as a token and converted with strtod rather than with the
// stream's own extractor, and that is a correction rather than a preference.
// `in >> v` on a double CANNOT PARSE "inf" OR "nan" in libstdc++: the first
// version of this filter used it and exited on exactly the two oracle cases
// whose values are not finite, a coefficient of infinity and a reference
// containing a not-a-number. Those two cases are the ones that exercise this
// project's own finite refusal, so the protocol has to be able to carry a value
// the implementation is going to reject. strtod accepts them, and is the same
// function Python's float() is built on, which is what makes the two sides of
// this protocol agree on what a token means.
bool read_doubles(std::istringstream &in, std::size_t count,
                  std::vector<double> &out)
{
    out.clear();
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::string token;
        if (!(in >> token)) {
            return false;
        }
        char *end = nullptr;
        const double v = std::strtod(token.c_str(), &end);
        // The whole token must be consumed, so "1.0junk" is refused rather than
        // silently read as one.
        if (end == token.c_str() || *end != '\0') {
            return false;
        }
        out.push_back(v);
    }
    return true;
}

bool read_int16s(std::istringstream &in, std::size_t count,
                 std::vector<std::int16_t> &out)
{
    out.clear();
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        long v = 0;
        if (!(in >> v) || v < -32768 || v > 32767) {
            return false;
        }
        out.push_back(static_cast<std::int16_t>(v));
    }
    return true;
}

void print_doubles(const std::vector<double> &values)
{
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::printf("%s%.17g", (i == 0) ? "" : " ", values[i]);
    }
    std::printf("\n");
}

void refused(p18::Error e)
{
    std::printf("REFUSED %s\n", p18::name(e));
}

int cascade_double(std::istringstream &in)
{
    std::size_t nstage = 0;
    std::size_t m = 0;
    if (!(in >> nstage >> m)) {
        return -1;
    }
    std::vector<double> coeffs;
    std::vector<double> samples;
    if (!read_doubles(in, nstage * p18::kCoeffPerStage, coeffs)
        || !read_doubles(in, m, samples)) {
        return -1;
    }
    std::vector<double> out;
    if (const auto bad = p18::biquad_df2t(coeffs, nstage, samples, out)) {
        refused(*bad);
        return 0;
    }
    print_doubles(out);
    return 0;
}

int cascade_single(std::istringstream &in)
{
    std::size_t nstage = 0;
    std::size_t m = 0;
    if (!(in >> nstage >> m)) {
        return -1;
    }
    std::vector<double> coeffs;
    std::vector<double> samples;
    if (!read_doubles(in, nstage * p18::kCoeffPerStage, coeffs)
        || !read_doubles(in, m, samples)) {
        return -1;
    }
    // Narrowed at the boundary, exactly as the C's caller does when it fills a
    // float array from the test's doubles.
    std::vector<float> fc;
    std::vector<float> fs;
    fc.reserve(coeffs.size());
    fs.reserve(samples.size());
    for (const double v : coeffs) {
        fc.push_back(static_cast<float>(v));
    }
    for (const double v : samples) {
        fs.push_back(static_cast<float>(v));
    }
    std::vector<float> out;
    if (const auto bad = p18::biquad_df2t(fc, nstage, fs, out)) {
        refused(*bad);
        return 0;
    }
    std::vector<double> wide;
    wide.reserve(out.size());
    for (const float v : out) {
        wide.push_back(static_cast<double>(v));
    }
    print_doubles(wide);
    return 0;
}

int post_shift(std::istringstream &in)
{
    std::size_t nstage = 0;
    if (!(in >> nstage)) {
        return -1;
    }
    std::vector<double> coeffs;
    if (!read_doubles(in, nstage * p18::kCoeffPerStage, coeffs)) {
        return -1;
    }
    std::int32_t shift = -99;
    if (const auto bad = p18::q15_post_shift(coeffs, nstage, shift)) {
        refused(*bad);
        return 0;
    }
    std::printf("shift=%d\n", static_cast<int>(shift));
    return 0;
}

int convert(std::istringstream &in)
{
    std::size_t nstage = 0;
    long shift = 0;
    if (!(in >> nstage >> shift)) {
        return -1;
    }
    std::vector<double> coeffs;
    if (!read_doubles(in, nstage * p18::kCoeffPerStage, coeffs)) {
        return -1;
    }
    std::vector<std::int16_t> out;
    if (const auto bad = p18::q15_coeffs(coeffs, nstage,
                                         static_cast<std::int32_t>(shift),
                                         out)) {
        refused(*bad);
        return 0;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::printf("%s%d", (i == 0) ? "" : " ", static_cast<int>(out[i]));
    }
    std::printf("\n");
    return 0;
}

int run_q15(std::istringstream &in)
{
    std::size_t nstage = 0;
    long shift = 0;
    std::size_t m = 0;
    if (!(in >> nstage >> shift >> m)) {
        return -1;
    }
    std::vector<std::int16_t> coeffs;
    std::vector<std::int16_t> samples;
    if (!read_int16s(in, nstage * p18::kCoeffPerStage, coeffs)
        || !read_int16s(in, m, samples)) {
        return -1;
    }
    std::vector<std::int16_t> out;
    std::uint32_t saturations = 0;
    if (const auto bad = p18::biquad_df1_q15(
            coeffs, nstage, static_cast<std::int32_t>(shift), samples, out,
            saturations)) {
        refused(*bad);
        return 0;
    }
    std::printf("sat=%u", saturations);
    for (const std::int16_t v : out) {
        std::printf(" %d", static_cast<int>(v));
    }
    std::printf("\n");
    return 0;
}

int metrics(std::istringstream &in)
{
    std::size_t n = 0;
    if (!(in >> n)) {
        return -1;
    }
    std::vector<double> got;
    std::vector<double> ref;
    if (!read_doubles(in, n, got) || !read_doubles(in, n, ref)) {
        return -1;
    }
    p18::Accept a{};
    if (const auto bad = p18::accept_metrics(got, ref, a)) {
        refused(*bad);
        return 0;
    }
    std::printf("rel_rms=%.17g snr_db=%.17g exact=%d\n", a.rel_rms, a.snr_db,
                a.exact ? 1 : 0);
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
        char verb = 0;
        if (!(in >> verb)) {
            std::printf("BAD_REQUEST\n");
            return 1;
        }
        int rc = 0;
        switch (verb) {
        case 'D': rc = cascade_double(in); break;
        case 'S': rc = cascade_single(in); break;
        case 'P': rc = post_shift(in); break;
        case 'Q': rc = convert(in); break;
        case 'R': rc = run_q15(in); break;
        case 'A': rc = metrics(in); break;
        default:
            std::printf("BAD_VERB %c\n", verb);
            return 1;
        }
        if (rc != 0) {
            std::printf("BAD_REQUEST\n");
            return 1;
        }
        // A request with anything left over is malformed. Ignoring the
        // remainder would let a caller send one thing and be answered about
        // another, which is the shape of failure P12's filter comment is
        // about: one request quietly turning into something else. It caught a
        // real one on Monday 26 October 2026, where a test built a
        // length-mismatch request this protocol cannot express and the extra
        // value would otherwise have been dropped in silence.
        std::string leftover;
        if (in >> leftover) {
            std::printf("BAD_REQUEST trailing %s\n", leftover.c_str());
            return 1;
        }
    }
    return 0;
}
