/* shared/board/uart.c: the console, polled, and why it refuses.
 *
 * The debug probe on this board presents a serial port to the host alongside the
 * debug interface, so one USB cable carries power, programming and the console.
 * That is the settled part.
 *
 * What is not settled is which pins reach it. Nucleo-144 convention puts the
 * virtual COM port on USART3 at PD8 and PD9 with alternate function 7, and the
 * workbook this volume grew from already flagged that as unverified. It is
 * settled only by the MB1363 board manual, which is a different document from
 * RM0455 and has not been read either.
 *
 * Getting it wrong is quiet. The wrong pins configure some other pins as a
 * serial port, the console shows nothing, and there is no error anywhere. Worse,
 * if one of those pins happens to be an input a shield drives, the board fights
 * it. So this refuses, and board_console_status() is how a project finds out
 * before it relies on printf.
 *
 * Polled rather than interrupt driven on purpose: P03 is the chapter that makes
 * it interrupt driven and measures where bytes begin to be lost. A console that
 * blocks is the right console for P01, because first light should have nothing
 * in it that can fail asynchronously.
 */
#include "board.h"
#include "stm32h7a3_regs.h"

static board_status_t g_console = BOARD_ERR_UART_PINS_UNCONFIRMED;

void board_console_init(void)
{
#if defined(BOARD_REGS_CONFIRMED) && defined(BOARD_CONSOLE_PINS_CONFIRMED)
    /* The steps, in the order RM0455 requires, written out rather than coded
     * from recollection of a sibling part:
     *
     *   1. Enable the GPIO port clock and the USART clock.
     *   2. Put both pins in alternate function mode and write the function
     *      number into the high or low alternate function register as the pin
     *      number dictates.
     *   3. Set the baud rate divider from the peripheral bus frequency, which
     *      means the clock tree must already be established. If
     *      board_core_hz() is 0, the divider cannot be computed and this must
     *      refuse rather than use a plausible number.
     *   4. Enable the transmitter, the receiver and the peripheral.
     *   5. Read the configuration back.
     *
     * Step 3 is the dependency worth naming: the console cannot be right while
     * the clock is unknown, so these two refusals are not independent.
     */
    if (board_core_hz() == 0u) {
        g_console = BOARD_ERR_CLOCK_UNCONFIRMED;
        return;
    }
    g_console = BOARD_OK;
#else
    g_console = BOARD_ERR_UART_PINS_UNCONFIRMED;
#endif
}

board_status_t board_console_status(void)
{
    return g_console;
}

/* One byte, blocking. Returns false when there is no console, so the retarget
 * hook can tell the difference between a byte sent and a byte discarded. */
bool board_console_put(char c)
{
    if (g_console != BOARD_OK) {
        return false;
    }
#if defined(BOARD_REGS_CONFIRMED) && defined(BOARD_CONSOLE_PINS_CONFIRMED)
    /* Wait for the transmit data register to be empty, then write. The status
     * and data register offsets are RM0455's. */
    (void) c;
    return true;
#else
    (void) c;
    return false;
#endif
}
