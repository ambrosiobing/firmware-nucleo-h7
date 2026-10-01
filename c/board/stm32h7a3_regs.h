/* c/board/stm32h7a3_regs.h: the few registers this board support touches.
 *
 * WHY THIS FILE EXISTS AND WHY IT REFUSES BY DEFAULT.
 *
 * The vendor pack's device header is deliberately not vendored into this
 * repository: that pack's licence differs file by file, and a project that
 * pasted it in would be redistributing terms it had not read. So the handful of
 * registers P01 needs are declared here instead.
 *
 * Every address below is **TO BE CONFIRMED against RM0455**, which documents
 * this part. RM0433 documents the STM32H743 and has a different map. None of
 * these has been read from RM0455, so none is asserted as fact, and the whole
 * file is inert until somebody reads the manual and defines
 * BOARD_REGS_CONFIRMED.
 *
 * That is the point rather than an inconvenience. A wrong peripheral base
 * address does not produce an error: it writes into some other peripheral, or
 * into nothing, and the symptom is an LED that does not light while the code
 * looks correct. Refusing to build is the only honest default.
 *
 * To bring this up:
 *   1. Open RM0455 and read the memory map chapter's peripheral table.
 *   2. Replace each PLACEHOLDER with the address, and delete its comment.
 *   3. Define BOARD_REGS_CONFIRMED, in this file, with the date you read it.
 *   4. Say in P01's README which manual revision you used.
 */
#ifndef STM32H7A3_REGS_H
#define STM32H7A3_REGS_H

#include <stdint.h>

/* Uncomment only after step 1 to 3 above. Until then every board function
 * returns a refusal and main reports it. */
/* #define BOARD_REGS_CONFIRMED "RM0455 rev N, read on <full date>" */

#define REG32(addr) (*(volatile uint32_t *) (addr))

/* ---------------------------------------------------------------- settled ---
 * These are architectural Cortex-M7 addresses, identical across the family and
 * not part-specific, so they are facts rather than placeholders. */
#define SCS_BASE        0xE000E000u
#define SYST_CSR        REG32(SCS_BASE + 0x010u)
#define SYST_RVR        REG32(SCS_BASE + 0x014u)
#define SYST_CVR        REG32(SCS_BASE + 0x018u)
#define SCB_BASE        0xE000ED00u
#define SCB_CPACR       REG32(SCB_BASE + 0x088u)   /* floating point enable */
#define DWT_BASE        0xE0001000u
#define DWT_CTRL        REG32(DWT_BASE + 0x000u)
#define DWT_CYCCNT      REG32(DWT_BASE + 0x004u)
#define DWT_LAR         REG32(0xE0001FB0u)         /* unlock, see P02 and P06 */
#define DEM_CR          REG32(0xE000EDFCu)

/* ------------------------------------------------------- to be confirmed ---
 * PLACEHOLDER means exactly that. The build refuses while these are in place.
 *
 * What to look for in RM0455: the RCC base, the PWR base, and the GPIO port
 * bases with their 0x400 stride. The LED ports are settled (PB0, PE1, PB14) and
 * the button is PC13, so once the GPIOA base and the stride are known, every
 * port this volume uses follows. */
#define BOARD_PLACEHOLDER 0u

#define RCC_BASE        BOARD_PLACEHOLDER   /* RM0455: reset and clock control */
#define PWR_BASE        BOARD_PLACEHOLDER   /* RM0455: power control, for the voltage scaling 280 MHz needs */
#define FLASH_BASE_REG  BOARD_PLACEHOLDER   /* RM0455: flash interface, for the latency 280 MHz needs */
#define GPIOA_BASE      BOARD_PLACEHOLDER   /* RM0455: first GPIO port; the rest are at a fixed stride */
#define GPIO_PORT_STRIDE BOARD_PLACEHOLDER  /* RM0455: usually 0x400, confirm it */

/* Once GPIOA_BASE and the stride are real, these are arithmetic rather than
 * further guesses. The port letters follow the board's own silkscreen. */
#define GPIO_PORT(n)    (GPIOA_BASE + (GPIO_PORT_STRIDE * (n)))
#define GPIOB           GPIO_PORT(1)
#define GPIOC           GPIO_PORT(2)
#define GPIOD           GPIO_PORT(3)
#define GPIOE           GPIO_PORT(4)

/* GPIO register offsets are architectural for this peripheral and the same
 * across the family, so these are not placeholders. BSRR is the one that
 * matters: a write to its low half sets a pin and to its high half resets it,
 * atomically and with no read-modify-write, which is why no interrupt can
 * corrupt a pin change. */
#define GPIO_MODER      0x00u
#define GPIO_OTYPER     0x04u
#define GPIO_OSPEEDR    0x08u
#define GPIO_PUPDR      0x0Cu
#define GPIO_IDR        0x10u
#define GPIO_ODR        0x14u
#define GPIO_BSRR       0x18u
#define GPIO_AFRL       0x20u
#define GPIO_AFRH       0x24u

#define GPIO_REG(port, off) REG32((port) + (off))

/* The LEDs and the button, which are settled board facts. */
#define LED_GREEN_PORT   GPIOB
#define LED_GREEN_PIN    0u      /* LD1 */
#define LED_YELLOW_PORT  GPIOE
#define LED_YELLOW_PIN   1u      /* LD2 */
#define LED_RED_PORT     GPIOB
#define LED_RED_PIN      14u     /* LD3 */
#define BUTTON_PORT      GPIOC
#define BUTTON_PIN       13u     /* the user button */

/* The console. The pins are Nucleo-144 convention and NOT read from the MB1363
 * board manual, which is a separate document from RM0455 and the only thing
 * that settles them. uart.c refuses on this. */
#define CONSOLE_USART_BASE BOARD_PLACEHOLDER  /* RM0455: USART3 */
#define CONSOLE_TX_PORT    GPIOD
#define CONSOLE_TX_PIN     8u    /* believed PD8, confirm in MB1363 */
#define CONSOLE_RX_PORT    GPIOD
#define CONSOLE_RX_PIN     9u    /* believed PD9, confirm in MB1363 */
#define CONSOLE_AF         7u    /* believed alternate function 7, confirm */

/* The reset clock. The part starts on its internal oscillator, and that is what
 * makes first light possible with no confirmed value at all. The frequency is
 * TO BE CONFIRMED: this family's internal oscillator is commonly 64 MHz, and
 * commonly is not a measurement. board_core_hz() returns 0 rather than this
 * number until it is confirmed, because a tick count without its rate is not a
 * time. */
#define HSI_HZ_UNCONFIRMED 64000000u

/* The target, which is settled arithmetic from settled facts: the debugger
 * supplies 8 MHz in bypass mode, and 8 / 2 times 140 / 2 gives 280 MHz. What is
 * not settled is the register sequence that achieves it. */
#define CORE_HZ_TARGET     280000000u

#endif /* STM32H7A3_REGS_H */
