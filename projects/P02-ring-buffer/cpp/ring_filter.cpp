// projects/P02-ring-buffer/cpp/ring_filter.cpp: P02's C++ ring as a filter.
//
// One operation per line on stdin, one answer per line on stdout. Four verbs:
//
//   I <mode>   discard the ring and start a fresh one in that ordering mode,
//              0 to 3. Answers "init mode=N cap=256".
//   P <hex>    put that byte. Answers "put=1 used=N" or "put=0 drops=N".
//   G          get. Answers "get=1 byte=XX used=N" or "get=0".
//   S          state. Answers "used=N drops=N head=N tail=N".
//
// One operation per line rather than a whole sequence per line, which is the
// opposite of the P08 filter and for a reason: P02's comparison runs to
// hundreds of thousands of operations, and a line that long is what broke the
// P08 filter on Saturday 3 October 2026. Short lines and many of them cost one
// process and no buffer question at all.
//
// `head` and `tail` are in the state answer because P02's arithmetic claim is
// about the free-running counters across their own wrap, not only about the
// occupancy derived from them. A version that masked the counters to the
// capacity would agree on `used` forever and disagree on `head` immediately.
//
// The Rust filter speaks the same four verbs, and
// python/tests/test_ring_parity.py drives both with the same code.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ring.hpp"

namespace {

// All four modes instantiated side by side, which is the thing the template
// buys over the C's four builds. Only one is live at a time; the others cost
// their storage and nothing else.
p02::Ring<p02::Barrier::None> ring0;
p02::Ring<p02::Barrier::Compiler> ring1;
p02::Ring<p02::Barrier::Dmb> ring2;
p02::Ring<p02::Barrier::AcqRel> ring3;

int live = 2;   // the C's default mode, so a run that forgets to send I matches it

void init(int mode)
{
    switch (mode) {
    case 0: ring0.init(); break;
    case 1: ring1.init(); break;
    case 2: ring2.init(); break;
    default: ring3.init(); break;
    }
    live = mode;
}

bool put(std::uint8_t byte)
{
    switch (live) {
    case 0: return ring0.put(byte);
    case 1: return ring1.put(byte);
    case 2: return ring2.put(byte);
    default: return ring3.put(byte);
    }
}

std::optional<std::uint8_t> get()
{
    switch (live) {
    case 0: return ring0.get();
    case 1: return ring1.get();
    case 2: return ring2.get();
    default: return ring3.get();
    }
}

struct State {
    std::uint32_t used, drops, head, tail;
};

State state()
{
    switch (live) {
    case 0: return {ring0.used(), ring0.drops(), ring0.head(), ring0.tail()};
    case 1: return {ring1.used(), ring1.drops(), ring1.head(), ring1.tail()};
    case 2: return {ring2.used(), ring2.drops(), ring2.head(), ring2.tail()};
    default: return {ring3.used(), ring3.drops(), ring3.head(), ring3.tail()};
    }
}

}  // namespace

int main()
{
    char line[64];

    while (std::fgets(line, sizeof line, stdin) != nullptr) {
        // A line that filled the buffer without reaching a newline is refused
        // rather than answered, for the reason the other filters now carry:
        // fgets would return the rest as a second request and this filter would
        // answer both. Every request here is a verb and at most two hex digits,
        // so 64 bytes is ample and the refusal should never fire.
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
        case 'I': {
            const int mode = std::atoi(line + 1);
            if (mode < 0 || mode > 3) {
                std::printf("BAD_MODE %d\n", mode);
                break;
            }
            init(mode);
            std::printf("init mode=%d cap=%u\n", mode, p02::ring_size);
            break;
        }

        case 'P': {
            const unsigned byte = static_cast<unsigned>(std::strtoul(line + 1, nullptr, 16));
            if (put(static_cast<std::uint8_t>(byte))) {
                std::printf("put=1 used=%u\n", state().used);
            } else {
                std::printf("put=0 drops=%u\n", state().drops);
            }
            break;
        }

        case 'G': {
            const std::optional<std::uint8_t> byte = get();
            if (byte) {
                std::printf("get=1 byte=%02X used=%u\n", *byte, state().used);
            } else {
                std::printf("get=0\n");
            }
            break;
        }

        case 'S': {
            const State s = state();
            std::printf("used=%u drops=%u head=%u tail=%u\n", s.used, s.drops, s.head, s.tail);
            break;
        }

        default:
            std::printf("BAD_VERB %c\n", line[0]);
            break;
        }
    }
    return EXIT_SUCCESS;
}
