/* shared/board/board.h: what P01 provides to every other project.
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

/* What SystemInit actually achieved, so a project can print it rather than
 * assume it. Returns BOARD_OK when the clock tree is at its configured target
 * and BOARD_ERR_CLOCK_UNCONFIRMED when the part is still on its reset clock. */
board_status_t board_clock_status(void);

/* The core frequency the code believes it is running at, in hertz, or 0 when
 * that is not established. Zero is the honest answer and every caller must
 * handle it: a tick count without its rate is not a time, and a guessed rate
 * scales every measured figure by an unknown factor. */
uint32_t board_core_hz(void);

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

/* A crude busy wait, in milliseconds, derived from board_core_hz(). When the
 * core frequency is not established this is approximate and the function says
 * so by returning false; it still delays, because a blinking LED is more useful
 * than a refusal here, but nothing may time anything with it. */
bool board_delay_ms(uint32_t ms);

#endif /* BOARD_H */
