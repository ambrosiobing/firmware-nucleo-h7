/* c/board/board.c: the LEDs, the button, and board_init.
 *
 * These are the settled facts of the board: LD1 green on PB0, LD2 yellow on PE1,
 * LD3 red on PB14, the user button on PC13, from two independent
 * machine-readable sources that name this exact board and agree. So the pin
 * numbers here are not guesses.
 *
 * What is still a guess is where the GPIO peripheral lives, which is RM0455's
 * business and is a placeholder in stm32h7a3_regs.h. Until that is read, every
 * function here is compiled out rather than writing to address zero, and
 * board_init reports what it could not do.
 *
 * A note on BSRR, because it is the one piece of reasoning worth keeping. A pin
 * is set by writing its bit to the low half of BSRR and reset by writing its bit
 * to the high half. That is a single store with no read-modify-write, so an
 * interrupt that changes another pin on the same port in the middle cannot
 * corrupt either change. Using ODR instead would be a read, a modify and a
 * write, and the lost update is the classic defect: two LEDs on one port, one of
 * them driven from an interrupt, and an occasional missed change that no test
 * reproduces.
 */
#include <stddef.h>

#include "board.h"
#include "stm32h7a3_regs.h"

#ifdef BOARD_REGS_CONFIRMED

static uint32_t led_port(board_led_t led)
{
    switch (led) {
    case BOARD_LED_GREEN:  return LED_GREEN_PORT;
    case BOARD_LED_YELLOW: return LED_YELLOW_PORT;
    case BOARD_LED_RED:    return LED_RED_PORT;
    }
    return LED_GREEN_PORT;
}

static uint32_t led_pin(board_led_t led)
{
    switch (led) {
    case BOARD_LED_GREEN:  return LED_GREEN_PIN;
    case BOARD_LED_YELLOW: return LED_YELLOW_PIN;
    case BOARD_LED_RED:    return LED_RED_PIN;
    }
    return LED_GREEN_PIN;
}

/* An input with a defined idle level. Needed by the button, and the reason it is
 * needed is worth stating where somebody will read it: a GPIO resets to input
 * mode with no pull at all, so an input pin that nothing drives does not read 0,
 * it reads whatever the surrounding circuit and leakage leave it at. That can be
 * stable, it can be either level, and it can change when a hand comes near the
 * board. ST's board support package configures this pin with an internal
 * pull-down, which says MB1363 fits no resistor of its own. */
/* A pin in alternate function mode, with the function number written into the
 * right half of the alternate function register.
 *
 * MOVED HERE FROM uart.c ON MONDAY 5 OCTOBER 2026 and made public, because
 * c/instr/freqcount.c became a second caller. Two copies of the rule below would
 * be two chances to get it wrong, and it is the kind of wrong that does not
 * report itself.
 *
 * Pins 0 to 7 use AFRL and pins 8 to 15 use AFRH, four bits each, which is why
 * the shift subtracts 8 above pin 7. Using AFRL for a pin above 7 writes the
 * function number onto a DIFFERENT pin entirely and leaves this one at function
 * 0, and neither pin reports anything about it. The console's PD8 and PD9 are
 * both in AFRH; the frequency counter's PD12 is too.
 *
 * The pull is an argument rather than a policy, because the two callers want it
 * for unrelated reasons: a serial line idles high, and ST's pulse counter
 * example asks for a pull-up on the counting input so an unconnected pin does
 * not float and count noise.
 *
 * Output speed is deliberately left at its reset value. ST asks for
 * GPIO_SPEED_FREQ_HIGH for the console and MEDIUM for the LPTIM input, and
 * neither pin is anywhere near needing either: the console changes state about
 * 115 thousand times a second and the counting pin is an input, where the speed
 * field governs the output driver and has nothing to do. Naming the reason beats
 * copying the setting. */
void board_pin_alternate(uint32_t port, uint32_t pin, uint32_t af, uint32_t pupd)
{
    /* Mode 10, alternate function, two bits per pin. */
    uint32_t moder = GPIO_REG(port, GPIO_MODER);
    moder &= ~(3u << (pin * 2u));
    moder |=  (2u << (pin * 2u));
    GPIO_REG(port, GPIO_MODER) = moder;

    /* Push-pull, set explicitly so that a second call after something else used
     * the pin does not inherit a different type. */
    GPIO_REG(port, GPIO_OTYPER) &= ~(1u << pin);

    uint32_t pupdr = GPIO_REG(port, GPIO_PUPDR);
    pupdr &= ~(3u << (pin * 2u));
    pupdr |=  (pupd << (pin * 2u));
    GPIO_REG(port, GPIO_PUPDR) = pupdr;

    const uint32_t reg   = (pin < 8u) ? GPIO_AFRL : GPIO_AFRH;
    const uint32_t shift = ((pin < 8u) ? pin : (pin - 8u)) * 4u;

    uint32_t afr = GPIO_REG(port, reg);
    afr &= ~(0xFu << shift);
    afr |=  (af   << shift);
    GPIO_REG(port, reg) = afr;
}

static void pin_input_pull(uint32_t port, uint32_t pin, uint32_t pupd)
{
    /* Mode 00, input, two bits per pin. */
    GPIO_REG(port, GPIO_MODER) &= ~(3u << (pin * 2u));

    uint32_t pupdr = GPIO_REG(port, GPIO_PUPDR);
    pupdr &= ~(3u << (pin * 2u));
    pupdr |=  (pupd << (pin * 2u));
    GPIO_REG(port, GPIO_PUPDR) = pupdr;
}

static void pin_output(uint32_t port, uint32_t pin)
{
    /* Mode 01, general purpose output, two bits per pin. */
    uint32_t moder = GPIO_REG(port, GPIO_MODER);
    moder &= ~(3u << (pin * 2u));
    moder |=  (1u << (pin * 2u));
    GPIO_REG(port, GPIO_MODER) = moder;

    /* Push-pull, and no pull resistor: the LED defines the level. */
    GPIO_REG(port, GPIO_OTYPER) &= ~(1u << pin);
    GPIO_REG(port, GPIO_PUPDR)  &= ~(3u << (pin * 2u));
}

void board_led_set(board_led_t led, bool on)
{
    const uint32_t port = led_port(led);
    const uint32_t pin  = led_pin(led);
    /* The atomic form. Low half sets, high half resets. */
    GPIO_REG(port, GPIO_BSRR) = on ? (1u << pin) : (1u << (pin + 16u));
}

void board_led_toggle(board_led_t led)
{
    const uint32_t port = led_port(led);
    const uint32_t pin  = led_pin(led);
    /* A toggle needs the current state, so this one is a read and a write. That
     * is safe here only because the consumer of each LED is single: nothing in
     * this volume toggles the same LED from two contexts. Said out loud because
     * it is the kind of assumption that stops being true quietly. */
    const bool on = (GPIO_REG(port, GPIO_ODR) & (1u << pin)) != 0u;
    board_led_set(led, !on);
}

bool board_button_pressed(void)
{
    /* A set bit means pressed. THIS POLARITY IS NOT SOURCED. PC13 as the user
     * button is settled, and the pin needs no configuring because a GPIO resets
     * to input mode with no pull, which is right if the board supplies its own
     * pull. Both of those are properties of MB1363 rather than of the die, so
     * neither the reference manual nor the device header settles them.
     *
     * Left as it is rather than guarded, because the cost of being wrong here is
     * only an inverted self-test and not a wrong measurement: if the board pulls
     * the other way, two LEDs sit lit until the button is held. That is
     * recognisable on sight, which is why this one is allowed to stand on an
     * assumption while the clock tree is not. */
    return (GPIO_REG(BUTTON_PORT, GPIO_IDR) & (1u << BUTTON_PIN)) != 0u;
}

void board_button_debug(uint32_t *moder, uint32_t *pupdr, uint32_t *idr)
{
    if (moder != NULL) { *moder = GPIO_REG(BUTTON_PORT, GPIO_MODER); }
    if (pupdr != NULL) { *pupdr = GPIO_REG(BUTTON_PORT, GPIO_PUPDR); }
    if (idr   != NULL) { *idr   = GPIO_REG(BUTTON_PORT, GPIO_IDR);   }
}

#else   /* the registers are not confirmed */

/* Inert rather than wrong. Writing to address zero would fault, and faulting is
 * not better than refusing: a fault at an LED call tells the reader nothing
 * about which manual to open. */
void board_pin_alternate(uint32_t port, uint32_t pin, uint32_t af, uint32_t pupd)
{ (void) port; (void) pin; (void) af; (void) pupd; }
void board_led_set(board_led_t led, bool on)    { (void) led; (void) on; }
void board_led_toggle(board_led_t led)          { (void) led; }
bool board_button_pressed(void)                 { return false; }
void board_button_debug(uint32_t *moder, uint32_t *pupdr, uint32_t *idr)
{
    if (moder != NULL) { *moder = 0u; }
    if (pupdr != NULL) { *pupdr = 0u; }
    if (idr   != NULL) { *idr   = 0u; }
}

#endif

void board_init(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;

    /* SystemInit has already run from Reset_Handler, so the clock is in whatever
     * state it could reach. This adds the pins and the console. */

#ifdef BOARD_REGS_CONFIRMED
    /* The port clocks, first, and this is the whole of first light's difficulty.
     *
     * A write to a GPIO register whose port clock is off is discarded. Not
     * refused, not faulted: discarded, silently, and a read returns the reset
     * value. So every line below this one can be correct, every address can be
     * right, and the board stays dark. There is nothing to see and nothing to
     * measure, and the natural conclusion is that the addresses are wrong, which
     * sends you back to the reference manual for a day.
     *
     * Three ports: B carries LD1 green on PB0 and LD3 red on PB14, E carries
     * LD2 yellow on PE1, and C carries the user button on PC13. All three sit in
     * the Smart Run Domain, so all three are enabled in RCC_AHB4ENR. The bit
     * positions come from the device header's own _Pos defines.
     *
     * The read-back is not decoration. The write crosses a bus bridge and takes
     * a few cycles to land, and an immediately following register write can be
     * issued before the clock is actually running. ST's own HAL reads the
     * register back for this reason in every one of its clock enable macros.
     * Without it this code would work or not work depending on compiler
     * optimisation level, which is the worst available outcome. */
    RCC_AHB4ENR |= (1u << RCC_AHB4ENR_GPIOBEN_POS)
                 | (1u << RCC_AHB4ENR_GPIOCEN_POS)
                 | (1u << RCC_AHB4ENR_GPIOEEN_POS);
    (void) RCC_AHB4ENR;

    pin_output(LED_GREEN_PORT,  LED_GREEN_PIN);
    pin_output(LED_YELLOW_PORT, LED_YELLOW_PIN);
    pin_output(LED_RED_PORT,    LED_RED_PIN);

    board_led_set(BOARD_LED_GREEN,  false);
    board_led_set(BOARD_LED_YELLOW, false);
    board_led_set(BOARD_LED_RED,    false);

    /* The button, which this function used to leave entirely alone. Pull-down,
     * on the authority of ST's board support package for this Nucleo, so the
     * released level is 0 and a press reads 1. Without it the released level was
     * undefined and the self-test could report a press that never happened. */
    pin_input_pull(BUTTON_PORT, BUTTON_PIN, GPIO_PUPD_PULLDOWN);
#endif

    /* Before the console, so that anything the console does can be measured, and
     * because the console's own init is the first thing in this file long enough
     * to be worth timing. */
    board_cycles_init();

    /* After the counter, because it measures with it. Order required. */
    board_delay_calibrate();

    board_console_init();
}
