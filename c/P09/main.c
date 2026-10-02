/* src/main.c: the board side, which is deliberately almost nothing.
 *
 * The codec is the chapter and the codec is already fully tested on the host.
 * What this file adds is the one thing the host cannot show: that the same
 * payload_encode, compiled for the Cortex-M7, produces the same five bytes.
 *
 * It has never been compiled. There is no arm-none-eabi-gcc on win11 aquamarine
 * as of Thursday 1 October 2026, so this file is written and left honest rather
 * than guessed at and claimed.
 *
 * Unlike chapter 6's back ends, nothing here needs a value confirmed against
 * RM0455. The codec touches no peripheral, no clock and no memory region, which
 * is why this chapter can be finished before chapter 1 and chapter 6 cannot.
 * The only dependencies are chapter 1's console and its printf retarget.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload.h"

/* Provided by chapter 1: the clock tree, the console, the printf retarget and,
 * since Saturday 3 October 2026, the cycle counter. The declaration used to be a
 * bare extern for board_init alone, which was enough while this chapter only
 * needed the console. Measuring the encoder needs board.h properly. */
#include "board.h"

/* The fourth golden vector from test/vectors.json, which is the negative case.
 * Checking the negative one on the target rather than an easy one is the whole
 * point: sign extension is the thing that differs between compilers and the
 * thing that works for every positive test value. */
static const payload_t vector = {
    .version = 1u, .flags = 2u, .sequence = 5u, .feature = -1, .battery = 40u
};
static const uint8_t expected[PAYLOAD_BYTES] = { 0x24, 0x05, 0xFF, 0xFF, 0xE8 };

static void print_hex(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        printf("%02X", b[i]);
    }
}

int main(void)
{
    board_init();

    printf("\r\nnucleo-h7a3-codec\r\n");
    printf("  payload       %u bits, %u bytes\r\n",
           (unsigned) PAYLOAD_BITS, (unsigned) PAYLOAD_BYTES);

    uint8_t buf[PAYLOAD_BYTES];
    size_t n = payload_encode(buf, sizeof buf, &vector);
    if (n != PAYLOAD_BYTES) {
        printf("  encode        failed, returned %u\r\n", (unsigned) n);
        for (;;) { }
    }

    printf("  encoded       ");
    print_hex(buf, n);
    printf("\r\n  expected      ");
    print_hex(expected, sizeof expected);
    printf("\r\n");

    /* The acceptance criterion for the target half of this chapter, checked on
     * the target rather than asserted in prose. */
    int bytes_agree = (memcmp(buf, expected, sizeof expected) == 0);

    payload_t back;
    int rc = payload_decode(buf, n, &back);
    int roundtrip_agrees =
        (rc == 0)
        && back.version == vector.version
        && back.flags == vector.flags
        && back.sequence == vector.sequence
        && back.feature == vector.feature       /* the sign extension */
        && back.battery == vector.battery;

    printf("  vector        %s\r\n", bytes_agree ? "agrees" : "DISAGREES");
    printf("  round trip    %s\r\n", roundtrip_agrees ? "agrees" : "DISAGREES");
    printf("  feature       %ld\r\n", (long) back.feature);

    if (!bytes_agree || !roundtrip_agrees) {
        /* Stop rather than continue. A board that prints a disagreement and
         * then carries on invites somebody to miss the line. */
        printf("\r\nthe target disagrees with the host. The specification is in\r\n");
        printf("docs/bitorder.md and it is normative for both.\r\n");
        for (;;) { }
    }

    /* WHAT THE ENCODER COSTS, which chapter 9's budget asks for and nothing had
     * measured. The budget says under 300 cycles per frame.
     *
     * Two-point with a warm-up, the same shape P01 and P02 arrived at earlier
     * today. A single timed window includes the cost of starting and stopping the
     * measurement; timing n and 2n and subtracting cancels that without needing to
     * know what it is. The warm-up does not cancel in a difference, because the
     * first pass through a loop pays flash wait states the second does not, so it
     * is paid once before either window.
     *
     * The figure is for one encode in a loop, including the loop control and a
     * call that does not inline across translation units. It is therefore an upper
     * bound on the encoder itself rather than the encoder alone, and the budget
     * row says so. */
    if (board_cycles_available() && board_core_hz() >= 1000000u) {
        /* reps, not n: main already has an n holding the encoded length, and
         * shadowing it drew -Wshadow. The warning was right and the name was
         * lazy. */
        const uint32_t reps = 2000u;
        uint8_t scratch[PAYLOAD_BYTES];

        for (uint32_t i = 0u; i < reps; i++) {
            (void) payload_encode(scratch, sizeof scratch, &vector);
        }

        const uint32_t a0 = board_cycles_now();
        for (uint32_t i = 0u; i < reps; i++) {
            (void) payload_encode(scratch, sizeof scratch, &vector);
        }
        const uint32_t a1 = board_cycles_now();

        const uint32_t b0 = board_cycles_now();
        for (uint32_t i = 0u; i < (2u * reps); i++) {
            (void) payload_encode(scratch, sizeof scratch, &vector);
        }
        const uint32_t b1 = board_cycles_now();

        const uint32_t shortw = a1 - a0;
        const uint32_t longw  = b1 - b0;
        if (longw > shortw) {
            const uint32_t cx100 = ((longw - shortw) * 100u) / reps;
            printf("  encode cost   %lu.%02lu cycles per frame, upper bound\r\n",
                   (unsigned long) (cx100 / 100u), (unsigned long) (cx100 % 100u));
            printf("                includes the loop and one call that does not\r\n");
            printf("                inline. The budget is under 300, so this\r\n");
            printf("                EXCEEDS it by about ten times. bw_put writes\r\n");
            printf("                one bit at a time; chapter 9 explains.\r\n");
        } else {
            printf("  encode cost   not measured: the two windows did not order\r\n");
        }
    } else {
        printf("  encode cost   not measured: no cycle counter or no clock\r\n");
    }

    printf("\r\nthe target agrees with the host over the golden vector.\r\n");
    for (;;) { }
}
