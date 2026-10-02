/* P01/main.c: first light, and an honest account of what is not established.
 *
 * This is the smallest program that proves a toolchain. It accounts for every
 * byte: a hand-written vector table, a hand-written reset handler, a linker
 * script whose every length came from this part's own documentation, and no
 * vendor generated code anywhere.
 *
 * WHAT IT DOES WHEN NOTHING IS CONFIRMED. It blinks, slowly, on the reset clock.
 * That is deliberate and it is the whole design of P01: the part starts on its
 * internal oscillator, and the LED pins are settled board facts, so an LED can
 * be made to blink without reading a single register address out of RM0455. A
 * blinking LED is therefore the first thing that works rather than the last, and
 * it proves the toolchain, the linker script, the vector table and the reset
 * handler all at once.
 *
 * WHAT IT REFUSES. The console, the 280 MHz clock, and any claim about time.
 * Each refusal is printed if there is a console to print it on, and signalled by
 * the LEDs if there is not, because a board with no console that merely sits
 * there tells the reader nothing.
 *
 * NEVER COMPILED. There is no arm-none-eabi-gcc on win11 aquamarine as of
 * Thursday 1 October 2026.
 */
#include <stdio.h>

#include "board.h"

/* The LED pattern encodes the state, so a board with no working console still
 * reports something. Green alone is the good case; the others are read by
 * counting, which is crude and is better than silence. */
static void report_by_led(board_status_t clock, board_status_t console)
{
    if (clock == BOARD_OK && console == BOARD_OK) {
        board_led_set(BOARD_LED_GREEN, true);       /* everything established */
        return;
    }
    if (clock != BOARD_OK) {
        board_led_set(BOARD_LED_RED, true);         /* the clock is not known */
    }
    if (console != BOARD_OK) {
        board_led_set(BOARD_LED_YELLOW, true);      /* no console */
    }
}

int main(void)
{
    board_init();

    const board_status_t clock = board_clock_status();
    const board_status_t console = board_console_status();
    const uint32_t hz = board_core_hz();

    /* These go nowhere when there is no console, and that is handled: _write
     * returns short and printf simply has no effect. The LEDs carry the same
     * information for exactly this case. */
    printf("\r\nnucleo-h7a3 P01, first light\r\n");

    if (hz == 0u) {
        /* Refusing to print a frequency is correct. A guessed rate would scale
         * every measured figure in every later project by an unknown factor. */
        printf("  core clock    not established\r\n");
        printf("                RM0455 governs the PLL, the flash latency and\r\n");
        printf("                the voltage scaling. None has been read.\r\n");
    } else {
        printf("  core clock    %lu Hz\r\n", (unsigned long) hz);
    }

    if (console != BOARD_OK) {
        printf("  console       not established, so you are not reading this\r\n");
    }

    printf("  LEDs          green PB0, yellow PE1, red PB14, all settled\r\n");
    printf("  button        PC13, settled\r\n");

    report_by_led(clock, console);

    /* The blink itself. Slow, and its period is deliberately not claimed in
     * milliseconds anywhere a reader might believe it: board_delay_ms returns
     * false when the core frequency is unknown, which it is, so the interval is
     * approximate and this is the only place that says so. */
    bool timed = true;
    for (;;) {
        board_led_toggle(BOARD_LED_GREEN);
        timed = board_delay_ms(500u);

        if (!timed) {
            /* Said once per visible cycle rather than once, so that somebody
             * who attaches a terminal later still learns it. */
            static uint32_t said;
            if ((said++ % 20u) == 0u) {
                printf("  note          the blink interval is approximate: the\r\n");
                printf("                core frequency is not established\r\n");
            }
        }

        /* The button is the one input, and holding it lights all three LEDs.
         * That is the whole self-test: if three LEDs light while the button is
         * held, then four settled pin facts and the GPIO configuration are all
         * right, and the first thing to doubt afterwards is something else.
         *
         * The else branch matters and was missing until Friday 2 October 2026.
         * Without it the yellow and red LEDs latched on at the first press and
         * never went out, so the test could be run exactly once per reset and
         * the board afterwards looked stuck. Holding has to mean holding, or the
         * self-test cannot be repeated and a reader cannot tell a working board
         * from a jammed one.
         *
         * On the polarity, which is NOT sourced: this reads a set bit as
         * pressed. If the MB1363 board wires B1 USER the other way, the symptom
         * is simply the whole test inverted, with yellow and red lit until the
         * button is held. That is still a pass for the pin facts, and it is
         * written here so it is recognised rather than diagnosed. */
        if (board_button_pressed()) {
            board_led_set(BOARD_LED_GREEN,  true);
            board_led_set(BOARD_LED_YELLOW, true);
            board_led_set(BOARD_LED_RED,    true);
        } else {
            /* Green is left alone: the blink above owns it. */
            board_led_set(BOARD_LED_YELLOW, false);
            board_led_set(BOARD_LED_RED,    false);
        }
    }
}
