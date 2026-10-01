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

/* Provided by chapter 1: the clock tree, the console and the printf retarget.
 * This chapter adds nothing to any of them. */
extern void board_init(void);

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

    printf("\r\nthe target agrees with the host over the golden vector.\r\n");
    for (;;) { }
}
