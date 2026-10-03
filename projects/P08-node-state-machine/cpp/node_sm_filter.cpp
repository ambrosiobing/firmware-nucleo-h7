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
//   frame=2001000528 unhandled=0
//
// all on one line. `rows=-` means no event was handled. The Rust filter speaks
// the same protocol, and python/tests/test_node_sm_parity.py drives both with
// the same code, so neither owns the event sequences: the test does.
//
// Unhandled events are counted here by counting the -1 returns rather than by
// reading a counter inside the implementation. Three of the four keep that
// counter in the context and the C keeps it in a file-scope variable, so
// counting the returns is the one way to ask all four the same question.
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

}  // namespace

int main()
{
    char line[4096];

    while (std::fgets(line, sizeof line, stdin) != nullptr) {
        line[std::strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        char* save = nullptr;
        char* token = std::strtok(line, " \t");
        if (token == nullptr) {
            continue;
        }

        p08::Ctx ctx{};
        ctx.pending_sample = static_cast<std::int32_t>(std::strtol(token, &save, 10));

        char rows[2048];
        std::size_t at = 0;
        int handled = 0;
        int unhandled = 0;

        while ((token = std::strtok(nullptr, " \t")) != nullptr) {
            p08::Event event{};
            if (!event_from_name(token, event)) {
                std::printf("BAD_EVENT %s\n", token);
                at = 0;
                unhandled = -1;   // marks the line as refused
                break;
            }
            const int row = p08::dispatch(ctx, event);
            if (row < 0) {
                ++unhandled;
            } else {
                ++handled;
            }
            // Every row, including the -1 for an ignored event, so the sequence
            // of answers lines up with the sequence of events one for one.
            at += static_cast<std::size_t>(
                std::snprintf(rows + at, sizeof rows - at, "%s%d", at == 0 ? "" : ",", row));
        }
        if (unhandled < 0) {
            continue;             // the BAD_EVENT line is already printed
        }
        if (at == 0) {
            std::snprintf(rows, sizeof rows, "-");
        }

        std::printf("state=%s rows=%s attempts=%u ok=%u dropped=%u seq=%u feature=%d frame=",
                    p08::name(ctx.state), rows, ctx.tx_attempts, ctx.cycles_ok,
                    ctx.cycles_dropped, ctx.sequence, ctx.feature);
        for (std::size_t i = 0; i < ctx.frame_len; ++i) {
            std::printf("%02X", ctx.frame[i]);
        }
        if (ctx.frame_len == 0) {
            std::printf("-");
        }
        std::printf(" unhandled=%d handled=%d\n", unhandled, handled);
    }
    return EXIT_SUCCESS;
}
