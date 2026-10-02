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
    /* Red means the frequency could not be established at all, which is a
     * narrower claim than "not BOARD_OK". Running at the reset speed is also not
     * BOARD_OK and is deliberately given no LED: the frequency is known in that
     * case, the console reports it with its provenance, and a third LED state
     * would be indistinguishable from the green heartbeat the loop drives. An
     * indicator that cannot be told apart from another indicator is not one. */
    if (clock == BOARD_ERR_CLOCK_UNCONFIRMED) {
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

    /* Three cases, not two, since Friday 2 October 2026. Collapsing the middle
     * one into the first would throw away a frequency that is genuinely known,
     * and collapsing it into the last would claim the 280 MHz target had been
     * reached. */
    if (hz == 0u) {
        /* Refusing to print a frequency is correct. A guessed rate would scale
         * every measured figure in every later project by an unknown factor. */
        printf("  core clock    not established\r\n");
        printf("                RM0455 governs the PLL, the flash latency and\r\n");
        printf("                the voltage scaling. None has been read.\r\n");
    } else if (clock == BOARD_CLOCK_AT_RESET_SPEED) {
        printf("  core clock    %lu Hz, the reset clock, NOT the 280 MHz target\r\n",
               (unsigned long) hz);
        printf("                decoded from RCC_CR, RCC_CFGR, RCC_CDCFGR1 and\r\n");
        printf("                RCC_CDCFGR2 rather than assumed. Nominal, so it\r\n");
        printf("                carries the oscillator's datasheet tolerance.\r\n");
        printf("  apb1 clock    %lu Hz, which is what a baud divider needs\r\n",
               (unsigned long) board_pclk1_hz());
    } else {
        printf("  core clock    %lu Hz, at the configured target\r\n",
               (unsigned long) hz);
        printf("  apb1 clock    %lu Hz\r\n", (unsigned long) board_pclk1_hz());
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
                /* The reason changed on Friday 2 October 2026 and the text had to
                 * follow it. The core frequency IS established now, decoded from
                 * the RCC registers, so blaming it here would be false. What is
                 * not established is how many core cycles one iteration of the
                 * delay loop costs, which depends on the compiler, the
                 * optimisation level, the cache state and the flash wait states.
                 * Naming the wrong cause in a diagnostic is worse than printing
                 * nothing, because somebody acts on it. */
                printf("  note          the blink interval is approximate. The core\r\n");
                printf("                frequency is known; the cycles per delay\r\n");
                printf("                loop iteration are estimated, not counted.\r\n");
                printf("                P06 is where that gets a real reference.\r\n");
            }
        }

        /* THE BUTTON TRACE, temporary, and to be deleted once the button is
         * explained. Friday 2 October 2026.
         *
         * board_button_pressed() reads 0 whether the button is held or free, with
         * RCC_AHB4ENR proving the GPIOC clock is on. Every attempt to diagnose it
         * so far read a register once, from the host, at a moment neither of us
         * could see, and had to be correlated with a press held across several
         * seconds of tool launches. That is a bad experiment and it produced three
         * inconclusive results.
         *
         * Printing the raw registers every cycle replaces it with a continuous
         * trace: hold the button, watch the numbers. What each column settles:
         *
         *   MODER bits 27:26 for pin 13, which must read 0 for input mode. If it
         *   is anything else the pin is not an input and nothing else matters.
         *
         *   PUPDR bits 27:26, which must read 2 for the pull-down that ST's board
         *   support package says this board relies on. If it reads 0 the write in
         *   board_init did not take, and the released level is undefined.
         *
         *   IDR in full, so bit 13 is visible in the context of all sixteen. All
         *   sixteen reading zero is itself suspicious for a port whose other pins
         *   go to floating headers. */
        {
            uint32_t m = 0u, pu = 0u, in = 0u;
            board_button_debug(&m, &pu, &in);
            printf("  pc13          MODER=%u PUPDR=%u IDR=0x%08lX bit13=%u\r\n",
                   (unsigned) ((m  >> 26) & 3u),
                   (unsigned) ((pu >> 26) & 3u),
                   (unsigned long) in,
                   (unsigned) ((in >> 13) & 1u));
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
