// projects/P03-interrupt-receive/cpp/attribute_filter.cpp: P03's attribution as a filter.
//
// One request per line on stdin, one answer per line on stdout. Three verbs:
//
//   A <rate> <sent> <accepted> <overruns> <dropped>
//        attribute that step and remember the row. Answers the nine fields, or
//        "REFUSED overaccounted".
//   F    the lowest-rate row where the target itself lost a byte, over the rows
//        remembered since the last R. Answers "first_loss=<index>" or
//        "first_loss=none".
//   R    forget the remembered rows. Answers "reset".
//
// Both functions are in the protocol because both are part of the claim. The
// per-step attribution decides what a row means; first_loss decides what a ramp
// means, and it is the one that must skip BRIDGE rows rather than naming the
// lowest bridge failure as a target limit.
//
// The Rust filter speaks the same three verbs, and
// python/tests/test_attribute_parity.py drives both with the same code.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "attribute.hpp"

namespace {

// Enough for any ramp a run records. A ramp is a handful of rates, not
// thousands, so a fixed array is the right shape and an overflow is a refusal.
constexpr std::size_t max_rows = 256;
p03::Row rows[max_rows];
std::size_t count = 0;

}  // namespace

int main()
{
    char line[256];

    while (std::fgets(line, sizeof line, stdin) != nullptr) {
        // A line that filled the buffer without reaching a newline is refused
        // rather than answered, the rule every filter here now carries: fgets
        // would return the rest as a second request and this would answer both.
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

        switch (line[0]) {
        case 'A': {
            unsigned long rate = 0, sent = 0, accepted = 0, overruns = 0, dropped = 0;
            if (std::sscanf(line + 1, "%lu %lu %lu %lu %lu",
                            &rate, &sent, &accepted, &overruns, &dropped) != 5) {
                std::printf("BAD_STEP\n");
                break;
            }
            const p03::Step step{
                static_cast<std::uint32_t>(rate),
                static_cast<std::uint32_t>(sent),
                static_cast<std::uint32_t>(accepted),
                static_cast<std::uint32_t>(overruns),
                static_cast<std::uint32_t>(dropped),
            };
            const std::optional<p03::Row> row = p03::attribute(step);
            if (!row) {
                std::printf("REFUSED overaccounted\n");
                break;
            }
            if (count < max_rows) {
                rows[count++] = *row;
            } else {
                std::fprintf(stderr, "more than %zu rows in one ramp\n", max_rows);
                return EXIT_FAILURE;
            }
            std::printf("rate=%u sent=%u delivered=%u overrun=%u dropped=%u "
                        "bridge_lost=%u target_lost=%u reached=%u verdict=%s\n",
                        row->rate, row->sent, row->delivered, row->overrun,
                        row->dropped, row->bridge_lost, row->target_lost,
                        row->reached, p03::name(row->verdict));
            break;
        }

        case 'F': {
            const std::optional<std::size_t> at = p03::first_loss(rows, count);
            if (at) {
                std::printf("first_loss=%zu\n", *at);
            } else {
                std::printf("first_loss=none\n");
            }
            break;
        }

        case 'R':
            count = 0;
            std::printf("reset\n");
            break;

        default:
            std::printf("BAD_VERB %c\n", line[0]);
            break;
        }
    }
    return EXIT_SUCCESS;
}
