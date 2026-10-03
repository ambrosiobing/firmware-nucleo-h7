// projects/P08-node-state-machine/cpp/node_sm_filter.cpp: P08's C++ table as a filter.
//
// One event sequence per line on stdin, one result per line on stdout. The
// first field is the sample a caller would have set before BLOCK, then the
// events by name:
//
//   5 TICK BLOCK FEATURE_DONE FRAME_READY TX_OK
//
// The answer names every row the dispatch took, which is the field that makes
// this a comparison of tables rather than of behaviour:
//
//   state=IDLE rows=0,5,8,10,12 attempts=1 ok=1 dropped=0 seq=1 feature=5
//   frame=2001000528 unhandled=0 handled=5
//
// all on one line. `rows=-` means no event was handled and `frame=-` that none
// was encoded. The Rust filter speaks the same protocol, and
// python/tests/test_node_sm_parity.py drives both with the same code, so
// neither owns the event sequences: the test does.
//
// WHY THE WHOLE OF STDIN IS READ AT ONCE, and this is a defect report rather
// than a preference. The first version read with fgets into char line[4096],
// which is how the other two filters in this repository are written. One of the
// parity sequences drives 512 complete cycles in a single line to reach the
// sequence number's wrap, which is about 21,500 characters, so fgets returned it
// in six pieces and this filter answered each piece as a separate request: 25
// answers for 20 requests. Nothing was wrong with the state machine and nothing
// would have been noticed if run_filter in conftest.py did not assert exactly
// one answer per request.
//
// A larger buffer would have moved the defect rather than removed it, because
// the protocol states no line limit. So the input is read whole and split on
// newlines, which is what the Rust filter already did and the reason it passed
// the same run. No iostream: the read is fread into a vector, the same cstdio
// discipline the other filters keep.
//
// Unhandled events are counted here from the -1 returns rather than read out of
// the implementation, because the four keep that counter in four places and
// counting the returns is the one way to ask all four the same question.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "node_sm.hpp"

namespace {

bool event_from_name(const char* text, p08::Event& out)
{
    struct Pair { const char* name; p08::Event event; };
    static const Pair names[] = {
        {"TICK", p08::Event::Tick},
        {"BLOCK", p08::Event::Block},
        {"FEATURE_DONE", p08::Event::FeatureDone},
        {"FRAME_READY", p08::Event::FrameReady},
        {"TX_OK", p08::Event::TxOk},
        {"TX_FAIL", p08::Event::TxFail},
        {"TIMEOUT", p08::Event::Timeout},
        {"BUTTON", p08::Event::Button},
        {"FAULT", p08::Event::Fault},
    };
    for (const Pair& p : names) {
        if (std::strcmp(p.name, text) == 0) {
            out = p.event;
            return true;
        }
    }
    return false;
}

// The whole of stdin, however long any one line is.
std::vector<char> read_all_input()
{
    std::vector<char> data;
    char chunk[65536];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, stdin)) > 0) {
        data.insert(data.end(), chunk, chunk + got);
    }
    data.push_back('\0');
    return data;
}

void answer(char* line)
{
    char* token = std::strtok(line, " \t");
    if (token == nullptr) {
        return;                       // a blank line asks nothing
    }

    p08::Ctx ctx{};
    ctx.pending_sample = static_cast<std::int32_t>(std::strtol(token, nullptr, 10));

    std::string rows;
    int handled = 0;
    int unhandled = 0;

    while ((token = std::strtok(nullptr, " \t")) != nullptr) {
        p08::Event event{};
        if (!event_from_name(token, event)) {
            std::printf("BAD_EVENT %s\n", token);
            return;
        }
        const int row = p08::dispatch(ctx, event);
        if (row < 0) {
            ++unhandled;
        } else {
            ++handled;
        }
        // Every row, including the -1 for an ignored event, so the answers line
        // up with the events one for one.
        if (!rows.empty()) {
            rows += ',';
        }
        rows += std::to_string(row);
    }
    if (rows.empty()) {
        rows = "-";
    }

    std::printf("state=%s rows=%s attempts=%u ok=%u dropped=%u seq=%u feature=%d frame=",
                p08::name(ctx.state), rows.c_str(), ctx.tx_attempts, ctx.cycles_ok,
                ctx.cycles_dropped, ctx.sequence, ctx.feature);
    for (std::size_t i = 0; i < ctx.frame_len; ++i) {
        std::printf("%02X", ctx.frame[i]);
    }
    if (ctx.frame_len == 0) {
        std::printf("-");
    }
    std::printf(" unhandled=%d handled=%d\n", unhandled, handled);
}

}  // namespace

int main()
{
    std::vector<char> data = read_all_input();

    char* at = data.data();
    while (*at != '\0') {
        char* end = std::strchr(at, '\n');
        if (end != nullptr) {
            *end = '\0';
        }
        // Strip a carriage return, so input written on Windows is read the same
        // way as input written anywhere else.
        const std::size_t len = std::strlen(at);
        if (len > 0 && at[len - 1] == '\r') {
            at[len - 1] = '\0';
        }
        if (*at != '\0') {
            answer(at);
        }
        if (end == nullptr) {
            break;                    // the last line had no newline
        }
        at = end + 1;
    }
    return EXIT_SUCCESS;
}
