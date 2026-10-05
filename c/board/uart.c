/* c/board/uart.c: the console, polled, and what it rests on.
 *
 * The debug probe on this board presents a serial port to the host alongside the
 * debug interface, so one USB cable carries power, programming and the console.
 * On win11 skyhorizon it enumerates as the STMicroelectronics STLink Virtual COM
 * Port, COM13.
 *
 * WHAT CHANGED ON FRIDAY 2 OCTOBER 2026. This file used to contain no register
 * writes at all, only the steps written out in prose, because it refused on two
 * independent grounds. Both are now answered, each by a cited source rather than
 * by convention:
 *
 *   The pins. Nucleo-144 convention puts the virtual COM port on USART3 at PD8
 *   and PD9 with alternate function 7, and the workbook this volume grew from
 *   flagged that as unverified. ST's own board support package for this Nucleo,
 *   Drivers/BSP/STM32H7xx_Nucleo/stm32h7xx_nucleo.h, states COM1_UART as USART3,
 *   COM1_TX_PIN as GPIO_PIN_8 on GPIOD, COM1_RX_PIN as GPIO_PIN_9 on GPIOD and
 *   both alternate functions as GPIO_AF7_USART3. Convention and fact agree here,
 *   which could not have been assumed.
 *
 *   The baud rate divider. It needs the peripheral bus frequency, which was
 *   unknown while board_core_hz() returned 0. The RCC registers were read off the
 *   running part and decoded, so board_pclk1_hz() now reports 64 MHz on the reset
 *   clock, and the divider is computed from that rather than from a plausible
 *   number.
 *
 * Getting the pins wrong would have been quiet. Wrong pins configure some other
 * pins as a serial port, the console shows nothing, and no error appears
 * anywhere. Worse, if one of them is an input that a shield drives, the board
 * fights it. That is why this waited for a source.
 *
 * Polled rather than interrupt driven on purpose: P03 is the chapter that makes
 * it interrupt driven and measures where bytes begin to be lost. A console that
 * blocks is the right console for P01, because first light should contain nothing
 * that can fail asynchronously. The one concession is a bounded wait rather than
 * an unbounded one, for the reason given at board_console_put.
 */
#include "board.h"
#include "stm32h7a3_regs.h"

static board_status_t g_console = BOARD_ERR_UART_PINS_UNCONFIRMED;

#if defined(BOARD_REGS_CONFIRMED) && defined(BOARD_CONSOLE_PINS_CONFIRMED)

/* THE ALTERNATE FUNCTION HELPER MOVED TO board.c ON MONDAY 5 OCTOBER 2026, as
 * board_pin_alternate, because c/instr/freqcount.c became a second caller and
 * the AFRL and AFRH split is the part of it worth having in one place. The pull
 * stays an argument, so this file still asks for a pull-up and still owns the
 * reason: a serial line idles HIGH, so a pin that is briefly undriven presents a
 * falling edge the receiver reads as a start bit and turns into one garbage
 * byte. */

#endif  /* the registers and the console pins are both confirmed */

void board_console_init(void)
{
#if defined(BOARD_REGS_CONFIRMED) && defined(BOARD_CONSOLE_PINS_CONFIRMED)
    /* The steps, in the order the code performs them, with what settles each one.
     * This list said something different until Saturday 3 October 2026 and the
     * code was reordered beneath it; a step list that contradicts its function is
     * the same defect as a comment that contradicts its return value, and this
     * repository paid for one of those the day before.
     *
     *   1. The GPIO port clock and the USART clock. A write to either peripheral
     *      before its clock runs is discarded in silence, exactly as it is for the
     *      LEDs in board.c.
     *   2. The divider, from the APB1 frequency. This is the dependency worth
     *      naming: the console cannot be right while the clock is unknown, so
     *      these two refusals were never independent.
     *   3. Transmitter, receiver, then the peripheral itself, with UE last because
     *      the configuration above must be in place before it starts.
     *   4. Read CR1 back, because a clock that is not running makes every write
     *      above a no-op that reports nothing.
     *   5. ONLY THEN both pins to alternate function 7, in AFRH because both are
     *      above 7, each with a pull-up. Doing this last is what stopped the
     *      garbage byte at every reset, for the reason set out below.
     */
    const uint32_t pclk = board_pclk1_hz();
    if (pclk == 0u) {
        /* The clock is not established, so no divider can be computed. Refusing
         * is the only honest move: a console at the wrong rate does not stay
         * silent, it emits convincing rubbish, and rubbish on a terminal gets
         * debugged as a wiring fault for an hour before anyone suspects the baud
         * rate. */
        g_console = BOARD_ERR_CLOCK_UNCONFIRMED;
        return;
    }

    RCC_AHB4ENR  |= (1u << RCC_AHB4ENR_GPIODEN_POS);
    RCC_APB1LENR |= (1u << RCC_APB1LENR_USART3EN_POS);
    (void) RCC_AHB4ENR;    /* read back: the write crosses a bus bridge */
    (void) RCC_APB1LENR;

    /* THE USART FIRST, THE PINS AFTERWARDS, and the order was the other way round
     * until Saturday 3 October 2026.
     *
     * Every reset emitted one garbage byte, printed by the terminal as a question
     * mark about ten milliseconds ahead of the report. It was noted as benign
     * several times before being explained, which is the habit this repository
     * spent the previous day learning not to indulge.
     *
     * The mechanism is entirely in this ordering. Switching PD8 to alternate
     * function connects it to the USART's transmit output, and a USART with UE
     * clear drives that output LOW. So the line fell from its idle high level and
     * stayed there for the microseconds the configuration below takes. The receiver
     * at the other end saw a falling edge, took it for a start bit, sampled the
     * following bit times, and delivered one byte of nonsense with a framing error.
     *
     * Configuring and enabling the USART first means the transmitter is already
     * idling high when the pin is connected to it, so there is no edge to
     * misread. The pull-ups below are the second half of the same argument: they
     * hold the line at its idle level in any window where nothing drives it,
     * including the one between reset and this function running.
     *
     * Harmless in itself, since one bad byte before the first line of output costs
     * nothing. It is fixed because an unexplained artefact that appears on every
     * single run trains a reader to ignore the console, and the next thing to
     * appear there might matter. */
    USART_REG(CONSOLE_USART_BASE, USART_CR1)   = 0u;
    USART_REG(CONSOLE_USART_BASE, USART_PRESC) = 0u;   /* prescaler 1, index 0 */
    USART_REG(CONSOLE_USART_BASE, USART_BRR)   =
        USART_BRR_FROM(pclk, CONSOLE_BAUD);

    USART_REG(CONSOLE_USART_BASE, USART_CR1) =
        (1u << USART_CR1_TE_POS) | (1u << USART_CR1_RE_POS);
    USART_REG(CONSOLE_USART_BASE, USART_CR1) |= (1u << USART_CR1_UE_POS);

    /* Read back, which is step 5 of the original list and is kept. If the
     * peripheral's clock were not running, CR1 would read 0 here however many
     * times it had been written, and that is worth catching at init rather than
     * discovering as a silent console. */
    if ((USART_REG(CONSOLE_USART_BASE, USART_CR1) & (1u << USART_CR1_UE_POS)) == 0u) {
        g_console = BOARD_ERR_UART_PINS_UNCONFIRMED;
        return;
    }

    /* Now the pins, with the transmitter already running and idling high. Pull-ups
     * on both: the transmit line so it never presents a false edge, and the
     * receive line so a floating input at this end cannot invent start bits when
     * the host is not transmitting. */
    board_pin_alternate(CONSOLE_TX_PORT, CONSOLE_TX_PIN, CONSOLE_AF, GPIO_PUPD_PULLUP);
    board_pin_alternate(CONSOLE_RX_PORT, CONSOLE_RX_PIN, CONSOLE_AF, GPIO_PUPD_PULLUP);

    g_console = BOARD_OK;
#else
    g_console = BOARD_ERR_UART_PINS_UNCONFIRMED;
#endif
}

board_status_t board_console_status(void)
{
    return g_console;
}

/* One byte, blocking but bounded. Returns false when the byte was not sent, so
 * the retarget hook can tell a byte sent from a byte discarded.
 *
 * The wait is bounded on purpose. An unbounded spin on a status bit is the
 * ordinary way to write this and it turns a misconfigured USART into a board that
 * hangs in printf before main does anything visible, with no LED and no output to
 * say why. A bounded wait degrades instead: the console loses bytes, the blink
 * carries on, and board_console_status() still says what it believes. The limit
 * is a loop count rather than a time because a time would need the very clock
 * this file is downstream of.
 */
bool board_console_put(char c)
{
    if (g_console != BOARD_OK) {
        return false;
    }
#if defined(BOARD_REGS_CONFIRMED) && defined(BOARD_CONSOLE_PINS_CONFIRMED)
    /* Bit 7 of ISR. ST's header calls it USART_ISR_TXE_TXFNF, one flag serving
     * two meanings depending on whether the FIFO is enabled. The FIFO is left
     * disabled here, its reset state, so this reads as plain transmit data
     * register empty. Looking for a bit named TXE finds nothing on this
     * peripheral. */
    uint32_t spins = 100000u;
    while ((USART_REG(CONSOLE_USART_BASE, USART_ISR)
            & (1u << USART_ISR_TXE_TXFNF_POS)) == 0u) {
        if (--spins == 0u) {
            return false;
        }
    }
    USART_REG(CONSOLE_USART_BASE, USART_TDR) = (uint32_t) (unsigned char) c;
    return true;
#else
    (void) c;
    return false;
#endif
}
