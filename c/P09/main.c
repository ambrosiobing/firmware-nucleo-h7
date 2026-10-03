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

/* One measurement of the encoder, in hundredths of a cycle per frame, or 0 when
 * the two timing windows did not come out in order and the result would be
 * meaningless.
 *
 * A function rather than inline code in main, and that is the whole trick. Called
 * twice from one image, it runs the same instructions at the same address both
 * times, so the only thing that differs between the two calls is the state of the
 * instruction cache. Written inline twice it would be two copies at two
 * addresses, which is the very confound this is meant to remove.
 *
 * reps is 2000, which at roughly 3000 cycles a frame is about six million cycles
 * per window, comfortably inside the 32 bit counter's 67 second wrap at 64 MHz and
 * long enough that the warm-up pass is a rounding error. */
static uint32_t measure_encode_cx100(const payload_t *v)
{
    const uint32_t reps = 2000u;
    uint8_t scratch[PAYLOAD_BYTES];

    for (uint32_t i = 0u; i < reps; i++) {
        (void) payload_encode(scratch, sizeof scratch, v);
    }

    const uint32_t a0 = board_cycles_now();
    for (uint32_t i = 0u; i < reps; i++) {
        (void) payload_encode(scratch, sizeof scratch, v);
    }
    const uint32_t a1 = board_cycles_now();

    const uint32_t b0 = board_cycles_now();
    for (uint32_t i = 0u; i < (2u * reps); i++) {
        (void) payload_encode(scratch, sizeof scratch, v);
    }
    const uint32_t b1 = board_cycles_now();

    const uint32_t shortw = a1 - a0;
    const uint32_t longw  = b1 - b0;
    return (longw > shortw) ? (((longw - shortw) * 100u) / reps) : 0u;
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

    /* WHAT THE ENCODER COSTS, measured twice in one image: once with the
     * instruction cache off, which is how the part comes out of reset, and once
     * with it on.
     *
     * WHY TWICE, and this is the point of the whole arrangement. On Friday 2
     * October 2026 this encoder was measured at 2954.98 cycles and then, after a
     * variable was renamed and three printf lines were added, at 3184.98. Nothing
     * about the encoder changed. The string literals grew, the code after them
     * moved, and with no cache every instruction is fetched from flash, so what
     * the loop cost depended on where the linker had put it.
     *
     * Comparing two builds can never separate a change from its placement. Two
     * calls to the same function in one image can: the code is at one address, the
     * build is one build, and nothing between the two measurements has moved by a
     * byte. The difference is the cache and nothing else.
     *
     * Each measurement is two-point with a warm-up, the shape P01 and P02 arrived
     * at. A single window includes the cost of starting and stopping it; timing
     * reps and 2 x reps and subtracting cancels that without needing to know what
     * it is. The warm-up is paid once before either window because a cold first
     * pass subtracts rather than cancels.
     *
     * The figure is one encode inside a loop, including the loop control and a
     * call that does not inline across translation units, so it is an upper bound
     * on the encoder rather than the encoder alone. */
    if (board_cycles_available() && board_core_hz() >= 1000000u) {
        const uint32_t cold = measure_encode_cx100(&vector);

        /* The cache, enabled between the two measurements and nowhere else. */
        const bool cached = board_icache_enable();
        const uint32_t warm = cached ? measure_encode_cx100(&vector) : 0u;

        if (cold == 0u) {
            printf("  encode cost   not measured: the two windows did not order\r\n");
        } else {
            printf("  encode cost   %lu.%02lu cycles per frame, I-cache OFF\r\n",
                   (unsigned long) (cold / 100u), (unsigned long) (cold % 100u));
            if (!cached) {
                printf("                I-cache would not enable: CCR.IC read back 0\r\n");
            } else if (warm == 0u) {
                printf("                I-cache ON: windows did not order\r\n");
            } else {
                printf("                %lu.%02lu cycles per frame, I-cache ON\r\n",
                       (unsigned long) (warm / 100u), (unsigned long) (warm % 100u));
                /* The ratio, in hundredths, so the reader does not have to divide
                 * two four-digit numbers in their head at a serial console. */
                const uint32_t ratio = (warm > 0u) ? ((cold * 100u) / warm) : 0u;
                printf("                the cache is worth %lu.%02lux here, same\r\n",
                       (unsigned long) (ratio / 100u), (unsigned long) (ratio % 100u));
                printf("                address, same build, nothing moved\r\n");
            }
            printf("                budget is under 300, so this EXCEEDS it by\r\n");
            printf("                about ten times either way. bw_put writes one\r\n");
            printf("                bit at a time; chapter 9 explains.\r\n");
        }
    } else {
        printf("  encode cost   not measured: no cycle counter or no clock\r\n");
    }

    printf("\r\nthe target agrees with the host over the golden vector.\r\n");
    for (;;) { }
}
