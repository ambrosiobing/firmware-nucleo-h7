/* shared/board/board.c: the LEDs, the button, and board_init.
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
    return (GPIO_REG(BUTTON_PORT, GPIO_IDR) & (1u << BUTTON_PIN)) != 0u;
}

#else   /* the registers are not confirmed */

/* Inert rather than wrong. Writing to address zero would fault, and faulting is
 * not better than refusing: a fault at an LED call tells the reader nothing
 * about which manual to open. */
void board_led_set(board_led_t led, bool on)    { (void) led; (void) on; }
void board_led_toggle(board_led_t led)          { (void) led; }
bool board_button_pressed(void)                 { return false; }

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
    /* The GPIO port clocks must be enabled before any port register is touched,
     * and a write to a port with its clock off is silently discarded, which
     * presents as an LED that never lights while the code looks right. The RCC
     * enable register and its bit positions are RM0455's, hence placeholders. */
    pin_output(LED_GREEN_PORT,  LED_GREEN_PIN);
    pin_output(LED_YELLOW_PORT, LED_YELLOW_PIN);
    pin_output(LED_RED_PORT,    LED_RED_PIN);

    board_led_set(BOARD_LED_GREEN,  false);
    board_led_set(BOARD_LED_YELLOW, false);
    board_led_set(BOARD_LED_RED,    false);
#endif

    board_console_init();
}
