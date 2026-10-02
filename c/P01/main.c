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
#include <stddef.h>
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
    /* Captured before the first printf, so it measures everything from the first
     * instruction of C up to the moment the first character is handed to the
     * USART. Read here rather than later because printf itself is slow: at 115200
     * baud each character costs about 87 microseconds, so a figure taken after
     * any output would be measuring the console rather than the startup. */
    const uint32_t startup_cycles = board_cycles_now();

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

    /* The cycle counter and what it measured, which is the one line in this
     * report that is a measurement taken on the part rather than a value read out
     * of a vendor file. */
    if (!board_cycles_available()) {
        printf("  cycle counter DWT_CYCCNT did not advance, so it is unavailable\r\n");
        printf("                Three writes can fail silently: TRCENA, the\r\n");
        printf("                CoreSight lock, and CYCCNTENA. Nothing is timed.\r\n");
    } else {
        const uint32_t ipm = board_delay_iters_per_ms();
        printf("  cycle counter DWT_CYCCNT running, ARM's own, not RM0455's\r\n");
        if (ipm == 0u) {
            printf("  delay         NOT calibrated, so intervals are approximate\r\n");
        } else {
            /* Fixed point to two places without pulling in floating point
             * formatting, which newlib-nano omits by default and which would cost
             * flash to add for one line of a report. */
            const uint32_t cyc_x100 = ((hz / 1000u) * 100u) / ipm;
            printf("  delay         %lu iterations/ms, measured = %lu.%02lu cycles each\r\n",
                   (unsigned long) ipm,
                   (unsigned long) (cyc_x100 / 100u),
                   (unsigned long) (cyc_x100 % 100u));
            printf("                two-point measurement at startup against\r\n");
            printf("                DWT_CYCCNT, so the fixed overhead of the\r\n");
            printf("                measurement itself cancels. A single-probe\r\n");
            printf("                The figure MOVES with the build: it was 9.00
");
            printf("                until four register writes were added to
");
            printf("                Reset_Handler, which shifted the loop in
");
            printf("                flash and doubled it. That is why this is
");
            printf("                measured every boot and never written down.
");
        }
    }

    /* Does the calibration actually produce the interval it was asked for?
     *
     * This checks the ARITHMETIC and not the clock, and the distinction matters.
     * Both the calibration and this measurement use DWT_CYCCNT, so a wrong core
     * frequency would cancel out and go unseen. What it does catch is the
     * multiplication overflowing, the iterations-per-millisecond figure being
     * scaled wrongly, and the delay taking a path other than the calibrated one.
     * Those are the plausible failures in the code just written; the clock was
     * settled separately and by other means. */
    if (board_cycles_available() && board_delay_iters_per_ms() != 0u && hz >= 1000000u) {
        const uint32_t want_ms = 100u;
        const uint32_t c0 = board_cycles_now();
        (void) board_delay_ms(want_ms);
        const uint32_t c1 = board_cycles_now();

        const uint32_t us = (c1 - c0) / (hz / 1000000u);
        printf("  delay check   asked %lu ms, measured %lu.%03lu ms by DWT_CYCCNT\r\n",
               (unsigned long) want_ms,
               (unsigned long) (us / 1000u),
               (unsigned long) (us % 1000u));
        printf("                checks the arithmetic, not the clock: both ends\r\n");
        printf("                use the same counter, so a wrong frequency cancels\r\n");
    }

    /* The third row of chapter 1's budget table, which said "not measured" from the
     * day it was written until Saturday 3 October 2026. */
    if (board_cycles_available() && hz >= 1000000u) {
        const uint32_t us = startup_cycles / (hz / 1000000u);
        printf("  startup       %lu cycles to the first character, %lu.%03lu ms\r\n",
               (unsigned long) startup_cycles,
               (unsigned long) (us / 1000u),
               (unsigned long) (us % 1000u));
        printf("                from the first instruction of C, so it excludes\r\n");
        printf("                only the reset sequence and the vector fetch.\r\n");
        printf("                Most of it is the delay calibration, which spins\r\n");
        printf("                40000 iterations on purpose.\r\n");
    }

    printf("  LEDs          green PB0, yellow PE1, red PB14, all settled\r\n");
    {
        /* Printed once, because a reader wants to know the pin was configured as
         * ST's board support package says this board needs and does not want it
         * repeated. MODER 0 is input, PUPDR 2 is pull-down. Measured active high
         * on Friday 2 October 2026: bit 13 reads 1 held and 0 free. */
        uint32_t bm = 0u, bp = 0u;
        board_button_debug(&bm, &bp, NULL);
        printf("  button        PC13, settled. MODER=%u PUPDR=%u, active high\r\n",
               (unsigned) ((bm >> 26) & 3u), (unsigned) ((bp >> 26) & 3u));
    }

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

        /* A periodic marker, on purpose this time.
         *
         * Until board_delay_ms started returning true there was a note printed
         * every twentieth cycle, and a host PC measured the blink period from its
         * timestamps. That note was a diagnostic about an approximation, so making
         * the delay honest removed it, and removed the only timing marker on the
         * wire with it. The cross-check went away as a side effect of a fix, which
         * is the kind of loss that is noticed much later.
         *
         * So this is a marker that exists to be measured rather than to warn. One
         * short line every twentieth cycle: about twenty characters, 1.7 ms on the
         * wire, 0.09 ms amortised per cycle, which is small and, more importantly,
         * known and subtractable. A host reading two consecutive ticks divides by
         * twenty and has the blink period from an instrument with nothing in common
         * with DWT_CYCCNT. */
        {
            static uint32_t ticks;
            if ((ticks % 20u) == 0u) {
                printf("  tick          %lu\r\n", (unsigned long) (ticks / 20u));
            }
            ticks++;
        }

        /* The button's state, printed only when it CHANGES.
         *
         * This replaced a trace that printed all three of PC13's registers on
         * every cycle. That trace did its job on Friday 2 October 2026 and was
         * removed the same day, for two reasons beyond tidiness. It added about
         * seventy characters per cycle, six milliseconds on the wire, which is
         * per-cycle overhead of exactly the kind that had to be subtracted out of
         * the afternoon's clock measurement. And two lines a second of unchanging
         * text is the condition in which a line that matters goes unread, which is
         * the mistake this repository made with a linker warning this morning.
         *
         * On change only: silent when nothing happens, loud at the moment of the
         * press, and no cost to any measurement. */
        {
            uint32_t in = 0u;
            board_button_debug(NULL, NULL, &in);
            const bool down = ((in >> 13) & 1u) != 0u;

            static bool last_down;
            static bool first = true;
            if (first || down != last_down) {
                first = false;
                last_down = down;
                printf("  pc13          %s  IDR=0x%08lX\r\n",
                       down ? "PRESSED " : "released",
                       (unsigned long) in);
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
