// projects/P06-timer-sampling/cpp/rate_filter.cpp: P06's witness as a filter.
//
// One request per line on stdin, one answer per line on stdout:
//
//   A <path> <fs> <nominal>   analyse that capture. Answers the fields, or
//                             "REFUSED <status>".
//
// THE CAPTURE COMES FROM A FILE AND NOT FROM THE LINE, which is the opposite of
// every other filter in this repository and deliberate twice over. A capture is
// thousands of doubles, so inline it would be a line of tens of kilobytes, which
// is exactly what broke the P08 filter. And a path is how the real witness
// receives a capture: `rate.py` takes one on its command line.
//
// The file is one value per line, blank lines and anything not starting like a
// number skipped, which is the same rule `rate.py`'s loader applies so a header
// row costs nothing.
//
// Doubles are printed with %.17g, which round-trips a double exactly. The parity
// test compares to a relative tolerance of 1e-12 and not bit for bit, for the
// reason rate.hpp gives, but printing fewer digits would have made the tolerance
// a property of the printing rather than of the arithmetic.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "rate.hpp"

namespace {

// Returns false when the file cannot be read at all.
bool load(const char* path, std::vector<double>& out)
{
    std::FILE* fh = std::fopen(path, "r");
    if (fh == nullptr) {
        return false;
    }
    char line[256];
    while (std::fgets(line, sizeof line, fh) != nullptr) {
        const char* at = line;
        while (*at == ' ' || *at == '\t') { ++at; }
        if (*at == '\0' || std::strchr("-+.0123456789", *at) == nullptr) {
            continue;              // a header or a comment
        }
        out.push_back(std::strtod(at, nullptr));
        if (out.size() >= p06::max_samples) {
            break;
        }
    }
    std::fclose(fh);
    return true;
}

}  // namespace

int main()
{
    char line[1024];

    while (std::fgets(line, sizeof line, stdin) != nullptr) {
        if (std::strchr(line, '\n') == nullptr && std::feof(stdin) == 0) {
            std::fprintf(stderr,
                         "a request line longer than %zu bytes was refused rather "
                         "than split\n",
                         sizeof line);
            return EXIT_FAILURE;
        }
        line[std::strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        if (line[0] != 'A') {
            std::printf("BAD_VERB %c\n", line[0]);
            continue;
        }

        char path[512] = {0};
        double fs = 0.0;
        double nominal = 0.0;
        if (std::sscanf(line + 1, "%511s %lf %lf", path, &fs, &nominal) != 3) {
            std::printf("BAD_REQUEST\n");
            continue;
        }

        std::vector<double> samples;
        if (!load(path, samples)) {
            std::printf("REFUSED no_such_capture\n");
            continue;
        }

        p06::Result r{};
        const p06::Status s = p06::analyse(samples.data(), samples.size(), fs, nominal, r);
        if (s != p06::Status::Ok) {
            std::printf("REFUSED %s edges=%zu\n", p06::name(s), r.edges);
            continue;
        }

        std::printf("edges=%zu duration_s=%.17g resolution_s=%.17g "
                    "count_rate_hz=%.17g fit_period_s=%.17g fit_rate_hz=%.17g "
                    "fit_rate_se_hz=%.17g interval_mean_s=%.17g interval_sd_s=%.17g "
                    "interval_worst_s=%.17g interval_worst_dev_s=%.17g "
                    "missing_edges=%zu routes_agree=%d rate_within_tolerance=%d "
                    "jitter_within_limit=%d worst_interval_within_limit=%d "
                    "no_missing_edges=%d pass=%d limited_by_instrument=%d\n",
                    r.edges, r.duration_s, r.resolution_s,
                    r.count_rate_hz, r.fit_period_s, r.fit_rate_hz,
                    r.fit_rate_se_hz, r.interval_mean_s, r.interval_sd_s,
                    r.interval_worst_s, r.interval_worst_dev_s,
                    r.missing_edges, r.routes_agree ? 1 : 0,
                    r.rate_within_tolerance ? 1 : 0,
                    r.jitter_within_limit ? 1 : 0,
                    r.worst_interval_within_limit ? 1 : 0,
                    r.no_missing_edges ? 1 : 0, r.pass ? 1 : 0,
                    r.limited_by_instrument ? 1 : 0);
    }
    return EXIT_SUCCESS;
}
