/* c/board/board.h: what P01 provides to every other project.
 *
 * Every project's main.c declares `extern void board_init(void)`. This is the
 * header behind that, and it is deliberately small: the clock tree, three LEDs,
 * one button and a console. Anything larger belongs to the project that needs
 * it.
 *
 * WHAT IS SETTLED AND WHAT IS NOT, because the difference decides what runs.
 *
 * Settled, from two independent machine-readable sources that name this exact
 * board and agree:
 *   - LD1 green on PB0, LD2 yellow on PE1, LD3 red on PB14
 *   - the user button on PC13
 *   - no Ethernet controller on this part
 *   - the 32.768 kHz crystal is fitted, so the real-time clock is usable
 *   - the high-speed clock arrives from the on-board debugger in bypass mode at
 *     8 MHz, and 8 / 2 times 140 / 2 gives 280 MHz
 *
 * Open, and therefore refused rather than guessed:
 *   - the RM0455 register fields for the PLL, the flash latency and the voltage
 *     scaling that 280 MHz needs
 *   - the virtual COM port pins, believed USART3 on PD8 and PD9 by Nucleo-144
 *     convention but not read from the MB1363 board manual
 *
 * The consequence is the shape of this interface. The part boots on its internal
 * oscillator, so an LED can blink using only settled facts, and that is what
 * P01 calls first light. Everything that needs an unconfirmed value returns an
 * error code instead of running, because a board that runs and lies is worse
 * than one that refuses: a clean square wave at the wrong rate is
 * indistinguishable from a right one without an external witness.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Why a function returned nothing useful. Negative values are failures and each
 * one names what to read, so a failure at the console tells the reader where to
 * look rather than only that something went wrong. */
typedef enum {
    BOARD_OK                   =  0,
    BOARD_CLOCK_AT_RESET_SPEED =  1,   /* frequency known, but not the target */
    BOARD_ERR_CLOCK_UNCONFIRMED = -1,  /* RM0455 PLL, latency, voltage scaling */
    BOARD_ERR_UART_PINS_UNCONFIRMED = -2,  /* MB1363 virtual COM port pins */
    BOARD_ERR_NOT_INITIALISED  = -3,
} board_status_t;

/* The three LEDs, by the colour printed on the board rather than by number, so
 * a wiring instruction and the code use the same word. */
typedef enum {
    BOARD_LED_GREEN  = 0,   /* LD1, PB0  */
    BOARD_LED_YELLOW = 1,   /* LD2, PE1  */
    BOARD_LED_RED    = 2,   /* LD3, PB14 */
} board_led_t;

/* Called by Reset_Handler before main and before any constructor. Brings the
 * part to a state where the LEDs work. It does NOT raise the clock to 280 MHz,
 * because that needs values this repository has not confirmed. */
void SystemInit(void);

/* What SystemInit actually achieved, so a project can print it rather than assume
 * it. Three outcomes, and the middle one was added on Friday 2 October 2026
 * because collapsing it into the third was costing the truth:
 *
 *   BOARD_OK                     at the configured 280 MHz target
 *   BOARD_CLOCK_AT_RESET_SPEED   frequency known and reported, but it is the
 *                                reset clock rather than the target
 *   BOARD_ERR_CLOCK_UNCONFIRMED  the frequency could not be established at all
 *
 * The distinction is the whole point. "Not at the target" and "unknown" are
 * different states, and a caller that must refuse to time anything cares about
 * the second and not the first. */
board_status_t board_clock_status(void);

/* The core frequency, in hertz, decoded from the RCC registers at startup rather
 * than hardcoded, so this function is equally truthful on the reset clock and on
 * the 280 MHz tree once that is written.
 *
 * Still 0 when the frequency could not be established, and every caller must
 * handle it: a tick count without its rate is not a time, and a guessed rate
 * scales every measured figure by an unknown factor.
 *
 * When non-zero it is nominal rather than measured, because the oscillator's own
 * frequency comes from the datasheet. Ask board_clock_status() for which clock it
 * is before quoting a figure derived from it. */
uint32_t board_core_hz(void);

/* The APB1 peripheral bus frequency, which is what a USART baud rate divider
 * needs and which is not in general the same as the core frequency. Separate
 * because deriving one from the other is the kind of silent assumption this
 * board support exists to avoid: the two are equal only while the prescalers are
 * at divide by one, which is true on the reset clock and will not be at 280 MHz.
 * 0 when not established. */
uint32_t board_pclk1_hz(void);

/* Full initialisation: clock, LEDs, console, printf retarget. Safe to call once.
 * Never fails: where a part of it cannot be configured, that part is left
 * unconfigured and the corresponding status function says so. A project that
 * needs the console checks board_console_status() rather than assuming. */
void board_init(void);

/* The LEDs, which work on the reset clock and need no confirmed value. */
void board_led_set(board_led_t led, bool on);
void board_led_toggle(board_led_t led);

/* The user button on PC13, polled. True while pressed. */
bool board_button_pressed(void);

/* The console. BOARD_ERR_UART_PINS_UNCONFIRMED until the pins are read from
 * MB1363, in which case printf goes nowhere and says so through this rather
 * than appearing to work. Note that the console also depends on the clock being
 * established, since the baud rate divider comes from it, so the two refusals
 * are not independent. */
void           board_console_init(void);
board_status_t board_console_status(void);

/* One byte, blocking. False when there is no console, so the printf retarget can
 * tell a byte sent from a byte discarded. */
bool board_console_put(char c);

/* The core's own cycle counter, DWT_CYCCNT, which is ARM's and not ST's.
 *
 * board_cycles_init() is called by board_init() and verifies that the counter
 * actually advances rather than assuming three register writes took effect.
 * board_cycles_available() reports that verdict, and every caller must consult it:
 * enabling the counter can fail silently in three separate ways, and a cycle
 * count of zero looks exactly like a very fast function.
 *
 * The counter is 32 bits of core clock cycles and wraps about every 67 seconds at
 * 64 MHz. Unsigned subtraction of two readings is correct across one wrap and
 * meaningless across two, so an interval longer than about a minute needs a
 * different instrument. */
void     board_cycles_init(void);
bool     board_cycles_available(void);
uint32_t board_cycles_now(void);

/* Measure what the delay loop actually costs, using the cycle counter, and keep
 * the result. Called by board_init() after board_cycles_init(), and that order is
 * required: calibration needs the counter running.
 *
 * board_delay_iters_per_ms() returns the measured iterations per millisecond, or
 * 0 when calibration could not be done, which is also what makes
 * board_delay_ms() return false. Dividing the core frequency in kilohertz by it
 * gives the measured cycles per iteration, which is the figure worth printing. */
void     board_delay_calibrate(void);
uint32_t board_delay_iters_per_ms(void);

/* The instruction cache, which is ARM's and not ST's, and which board_init
 * deliberately leaves off.
 *
 * It is off at reset. Every cycle figure published in this volume before Friday
 * 2 October 2026 was measured in that state, and three of them moved because of
 * edits that changed no logic: with no cache, every instruction is fetched from
 * flash and what a loop costs depends on where the linker put it.
 *
 * board_init does not enable it, for two reasons. Enabling it there would
 * silently invalidate every figure already printed. And leaving it off makes a
 * better experiment available: a single image can measure a function cold, call
 * board_icache_enable(), and measure the same function again at the same address
 * in the same build. Nothing has moved, so the difference is the cache. Two
 * builds could never establish that.
 *
 * board_icache_enable() returns the state read back from the register rather
 * than the fact of having written to it, and false means the cache is not on.
 *
 * There is no disable, on purpose: reset already provides the off state, and the
 * disable sequence in ARM's header has not been read.
 *
 * ONE ORDERING CONSEQUENCE. board_delay_calibrate() measures the delay loop in
 * whatever cache state holds when it runs, which is off. A project that enables
 * the cache and then needs an accurate millisecond must call
 * board_delay_calibrate() again, because the loop it measured is now faster than
 * the measurement says. */
bool board_icache_enable(void);
bool board_icache_enabled(void);

/* The button's three registers, for diagnosis rather than for use.
 *
 * board_button_pressed() answers a yes or no question and that answer has been
 * 0 whether the button was held or free, with the port clock proven on. A yes or
 * no cannot say which of several things is wrong, so this exposes the raw state:
 * the mode bits, the pull bits and the input register, straight from the port.
 *
 * Any of the three pointers may be NULL. All three are set to 0 when the
 * registers are not confirmed, which is indistinguishable from a port that reads
 * zero, so a caller that cannot tell those apart should ask
 * board_clock_status() instead of guessing.
 *
 * KEPT after the button was explained on Friday 2 October 2026, which is a
 * change of mind worth recording. It was written to be deleted once PC13 was
 * understood, and it settled that question in one capture after three one-shot
 * register reads had settled nothing: MODER 0, PUPDR 2, and bit 13 reading 1 held
 * and 0 free with a clean transition. Having proved that a continuous trace beats
 * a timed read, removing the means of taking one would be the wrong lesson to
 * draw. P01 now prints the state only when it changes, so it costs nothing per
 * cycle. */
void board_button_debug(uint32_t *moder, uint32_t *pupdr, uint32_t *idr);

/* A crude busy wait, in milliseconds, derived from board_core_hz(). When the
 * core frequency is not established this is approximate and the function says
 * so by returning false; it still delays, because a blinking LED is more useful
 * than a refusal here, but nothing may time anything with it. */
bool board_delay_ms(uint32_t ms);

#endif /* BOARD_H */
