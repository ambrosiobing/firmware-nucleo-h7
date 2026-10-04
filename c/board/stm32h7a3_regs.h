/* c/board/stm32h7a3_regs.h: the few registers this board support touches.
 *
 * WHY THIS FILE EXISTS AND WHY IT REFUSES BY DEFAULT.
 *
 * The vendor pack's device header is deliberately not vendored into this
 * repository: that pack's licence differs file by file, and a project that
 * pasted it in would be redistributing terms it had not read. So the handful of
 * registers P01 needs are declared here instead.
 *
 * WHICH AUTHORITY SETTLED THESE, which is not the one this file first asked for.
 *
 * Every address below was unconfirmed until Friday 2 October 2026. They were
 * then read, not from RM0455 itself, but from ST's own CMSIS device header for
 * this exact die, stm32h7a3xxq.h, as shipped in STM32Cube_FW_H7_V1.13.0. That
 * is a real authority and a different one, so it is named here rather than
 * quietly standing in for the manual: it is ST's machine-readable statement of
 * the same memory map, it is specific to the Q package variant, and anyone with
 * the pack can re-derive it in one command. What it cannot settle is anything
 * not expressed as an address or a bit position, so the clock configuration
 * sequence for 280 MHz is still RM0455's and is still refused.
 *
 * Reading them from the header rather than from the manual also produced the
 * clearest evidence of this volume's central trap, and it is worth recording
 * where that was found. On this die the peripheral bases are offsets from
 * SRD_AHB4PERIPH_BASE and CD_APB1PERIPH_BASE. This part has two power domains,
 * CD for the CPU domain and SRD for the Smart Run Domain. The STM32H743 that
 * almost every STM32H7 tutorial is written against has three, named D1, D2 and
 * D3, and the correspondence is not a rename: what the H743 calls D3_AHB1 this
 * part calls SRD_AHB4, and what it calls D1_AHB1 this part calls CD_AHB3. ST
 * kept the old names only as trailing comments. Of the D-prefixed defines just
 * one survives in the whole header, D1_AXISRAM_BASE, and that is a memory alias
 * with nothing to do with peripheral domains. So code copied from an H743
 * project does not compute a wrong address here, it fails to resolve the symbol
 * at all, which is the kindest way this trap can present.
 *
 * WHY IT STILL REFUSES WHERE IT REFUSES. A wrong peripheral base address does
 * not produce an error: it writes into some other peripheral, or into nothing,
 * and the symptom is an LED that does not light while the code looks correct.
 * So anything not settled by a cited authority is still a placeholder and still
 * compiled out. The console pins are the live example. USART3's base address is
 * settled, but whether this board wires the probe's virtual serial port to PD8
 * and PD9 at alternate function 7 is a property of the MB1363 board rather than
 * of the die, so the device header cannot answer it and uart.c still refuses.
 */
#ifndef STM32H7A3_REGS_H
#define STM32H7A3_REGS_H

#include <stdint.h>

/* Confirmed Friday 2 October 2026 against the authority named above. The string
 * is deliberately the provenance rather than a version number, so that anybody
 * reading a register write can see what it rests on. */
#define BOARD_REGS_CONFIRMED \
    "ST CMSIS stm32h7a3xxq.h, STM32Cube_FW_H7_V1.13.0, read Friday 2 October 2026"

#define REG32(addr) (*(volatile uint32_t *) (addr))

/* ---------------------------------------------------------------- settled ---
 * These are architectural Cortex-M7 addresses, identical across the family and
 * not part-specific, so they are facts rather than placeholders. */
#define SCS_BASE        0xE000E000u
#define SYST_CSR        REG32(SCS_BASE + 0x010u)
#define SYST_RVR        REG32(SCS_BASE + 0x014u)
#define SYST_CVR        REG32(SCS_BASE + 0x018u)
/* SysTick's control bits, and like the DWT above these are ARM's rather than
 * ST's: the ARMv7-M architecture defines them identically on every Cortex-M7, so
 * RM0455 has nothing to say about them. Taken from CMSIS core_cm7.h in
 * STM32Cube_FW_H7_V1.13.0, read on Saturday 3 October 2026, which states
 * SysTick_CTRL_ENABLE_Pos 0, TICKINT_Pos 1, CLKSOURCE_Pos 2, COUNTFLAG_Pos 16,
 * and a 24 bit reload field.
 *
 * CLKSOURCE selects the core clock rather than the implementation-defined external
 * reference, which is the only one of the three whose meaning is not obvious from
 * its name and the only one a reader might reasonably leave clear by accident. */
#define SYST_CSR_ENABLE_POS     0u
#define SYST_CSR_TICKINT_POS    1u
#define SYST_CSR_CLKSOURCE_POS  2u
#define SYST_CSR_COUNTFLAG_POS  16u
#define SYST_RELOAD_MAX         0xFFFFFFu   /* the reload field is 24 bits */

#define SCB_BASE        0xE000ED00u
#define SCB_CPACR       REG32(SCB_BASE + 0x088u)   /* floating point enable */
#define DWT_BASE        0xE0001000u
#define DWT_CTRL        REG32(DWT_BASE + 0x000u)
#define DWT_CYCCNT      REG32(DWT_BASE + 0x004u)
#define DWT_LAR         REG32(0xE0001FB0u)         /* unlock, see P02 and P06 */
#define DEM_CR          REG32(0xE000EDFCu)

/* The three bits and one key that start the cycle counter. All ARM's, from the
 * ARMv7-M architecture reference manual rather than from RM0455, which is why
 * they are usable while most of this file's peripheral values were not.
 *
 * DWT_LAR_KEY is a CoreSight software lock. Some implementations require it
 * before DWT_CTRL accepts a write and some have no such register, so it is
 * written unconditionally: the cost is one instruction and the alternative is
 * researching which case applies here. */
#define DEM_CR_TRCENA_POS        24u
#define DWT_CTRL_CYCCNTENA_POS    0u
#define DWT_LAR_KEY             0xC5ACCE55u

/* THE INSTRUCTION CACHE, and it is ARM's too.
 *
 * Read on Friday 2 October 2026 from ARM's own CMSIS headers, in the Cube pack
 * at Drivers/CMSIS/Include, and from nothing of ST's:
 *
 *   core_cm7.h names the bits in the Configuration and Control register and
 *   places CCSIDR at offset 0x080 and ICIALLU at offset 0x250 from SCB_BASE.
 *
 *   cachel1_armv7.h, which core_cm7.h includes only when the device header
 *   declares a cache present, holds the enable, disable and invalidate
 *   sequences. It is where they live in CMSIS 5.6 and later; they used to be in
 *   core_cm7.h, which is why a search of that file alone finds nothing.
 *
 *   stm32h7a3xxq.h declares __ICACHE_PRESENT 1 and __DCACHE_PRESENT 1 on a
 *   Cortex-M7 r1p2, so the gate opens and this part has both.
 *
 * The line length is 32 bytes, fixed by the architecture for Cortex-M7 rather
 * than chosen by ST: cachel1_armv7.h defines both __SCB_ICACHE_LINE_SIZE and
 * __SCB_DCACHE_LINE_SIZE as 32 and says the figure is fixed for this core.
 *
 * ONE TRAP WORTH THE SPACE. core_cm7.h defaults __ICACHE_PRESENT and
 * __DCACHE_PRESENT to 0 when the device header does not define them, and says so
 * with a #warning and nothing more. A build that does not read its warnings
 * would compile, link, run, and quietly never enable a cache, with every cache
 * call compiled away to an empty function body. That is the same shape as the
 * RM0433 trap at the top of this file: the wrong answer arrives working.
 *
 * Only the instruction cache is used in this repository so far. The data cache
 * is declared present and is deliberately left alone, because enabling it
 * changes what a buffer shared with a bus master means, and that question
 * belongs with the transfer engine that no confirmed register yet describes. */
#define SCB_CCR         REG32(SCB_BASE + 0x014u)   /* configuration and control */
#define SCB_CCSIDR      REG32(SCB_BASE + 0x080u)   /* cache size id, read only */
#define SCB_CCSELR      REG32(SCB_BASE + 0x084u)   /* cache size selection */
#define SCB_ICIALLU     REG32(SCB_BASE + 0x250u)   /* invalidate all, write only */

#define SCB_CCR_DC_POS          16u   /* data cache enable */
#define SCB_CCR_IC_POS          17u   /* instruction cache enable */
#define SCB_CCR_BP_POS          18u   /* branch prediction enable */
#define SCB_CACHE_LINE_BYTES    32u   /* fixed for Cortex-M7 by the architecture */

/* ------------------------------------------------------------- peripherals ---
 * Confirmed. Each line carries the expression the device header gave as well as
 * the value it resolves to, because a bare hexadecimal number here would be the
 * unsourced constant this file exists to avoid. The domain roots are
 *
 *   PERIPH_BASE          0x40000000
 *   CD_AHB3PERIPH_BASE   PERIPH_BASE + 0x12000000  = 0x52000000
 *   SRD_AHB4PERIPH_BASE  PERIPH_BASE + 0x18020000  = 0x58020000
 */
#define BOARD_PLACEHOLDER 0u    /* kept, for anything not yet settled */

#define RCC_BASE        0x58024400u  /* SRD_AHB4PERIPH_BASE + 0x4400 */
#define PWR_BASE        0x58024800u  /* SRD_AHB4PERIPH_BASE + 0x4800 */
#define FLASH_BASE_REG  0x52002000u  /* CD_AHB3PERIPH_BASE + 0x2000, the flash
                                      * INTERFACE registers. Not 0x08000000,
                                      * which is where flash is readable. The
                                      * header calls the two FLASH_R_BASE and
                                      * FLASH_BANK1_BASE, and confusing them
                                      * treats the program's first instruction
                                      * as a control register. */
#define GPIOA_BASE      0x58020000u  /* SRD_AHB4PERIPH_BASE + 0x0000 */
#define GPIO_PORT_STRIDE 0x400u      /* Not assumed. The header states GPIOA at
                                      * +0x000, GPIOB +0x400, GPIOC +0x800 and
                                      * GPIOE +0x1000, so the stride is
                                      * arithmetic from four given values rather
                                      * than the usual-case folklore this line
                                      * used to carry. */

/* The RCC registers this board support touches, by offset from RCC_BASE. These
 * are not defines in the device header: they are member positions within
 * RCC_TypeDef, so they were computed from that structure's declaration order
 * including every RESERVED word. The arithmetic is written out because one
 * miscounted RESERVED array moves every later register and the failure is
 * silent.
 *
 *   CKGAENR at 0x0B0, then RESERVED10[31] spans 0x0B4 to 0x12F, then
 *   RSR 0x130, AHB3ENR 0x134, AHB1ENR 0x138, AHB2ENR 0x13C, AHB4ENR 0x140,
 *   APB3ENR 0x144, APB1LENR 0x148 */
#define RCC_CR          REG32(RCC_BASE + 0x000u)   /* oscillators and the PLL */
#define RCC_CFGR        REG32(RCC_BASE + 0x010u)   /* which source drives sys_ck */
#define RCC_CDCFGR1     REG32(RCC_BASE + 0x018u)   /* CPU and AHB prescalers */
#define RCC_CDCFGR2     REG32(RCC_BASE + 0x01Cu)   /* APB1 and APB2 prescalers */
#define RCC_AHB4ENR     REG32(RCC_BASE + 0x140u)   /* the GPIO port clocks */
#define RCC_APB1LENR    REG32(RCC_BASE + 0x148u)   /* USART3's clock */

/* The clock tree fields, every one of them taken from a named define in ST's
 * device header rather than from what is true across the STM32H7 family. The
 * distinction matters here more than anywhere: this is the register set whose
 * layout differs between RM0455 and RM0433, and the H743 calls these same
 * registers D1CFGR and D1PPRE1 rather than CDCFGR1 and CDPPRE1.
 *
 * Read from the running part on Friday 2 October 2026, which is how the reset
 * tree below was established rather than assumed. */
#define RCC_CR_HSIDIV_POS       3u
#define RCC_CR_HSIDIV_MSK       (3u << RCC_CR_HSIDIV_POS)
#define RCC_CR_HSIDIVF_MSK      (1u << 5)       /* the ratio above is in effect */
#define RCC_CR_HSEON_MSK        (1u << 16)
#define RCC_CR_PLL1ON_MSK       (1u << 24)

#define RCC_CFGR_SWS_POS        3u
#define RCC_CFGR_SWS_MSK        (7u << RCC_CFGR_SWS_POS)
#define RCC_SWS_HSI             0u      /* RCC_CFGR_SWS_HSI  = 0x00 */
#define RCC_SWS_CSI             1u      /* RCC_CFGR_SWS_CSI  = 0x08 */
#define RCC_SWS_HSE             2u      /* RCC_CFGR_SWS_HSE  = 0x10 */
#define RCC_SWS_PLL1            3u      /* RCC_CFGR_SWS_PLL1 = 0x18 */

#define RCC_CDCFGR1_HPRE_POS    0u      /* the AHB prescaler */
#define RCC_CDCFGR1_HPRE_MSK    (0xFu << RCC_CDCFGR1_HPRE_POS)
#define RCC_CDCFGR1_CDCPRE_POS  8u      /* the CPU prescaler */
#define RCC_CDCFGR1_CDCPRE_MSK  (0xFu << RCC_CDCFGR1_CDCPRE_POS)
#define RCC_CDCFGR2_CDPPRE1_POS 4u      /* the APB1 prescaler */
#define RCC_CDCFGR2_CDPPRE1_MSK (7u << RCC_CDCFGR2_CDPPRE1_POS)

/* The divider encodings, and only the ones ST's header names. The AHB and CPU
 * fields are four bits with a sparse encoding, and the header gives DIV1, DIV2
 * and DIV4 as 0x0, 0x8 and 0x9. Later ratios exist and are deliberately NOT
 * written here from recollection of how the family usually encodes them: the
 * decoder refuses on any field value not in this list and names the register and
 * the value, so extending it means reading three more lines of the header rather
 * than debugging a frequency that is wrong by a factor of two. */
#define RCC_AHBPRE_DIV1         0x0u
#define RCC_AHBPRE_DIV2         0x8u
#define RCC_AHBPRE_DIV4         0x9u
#define RCC_APBPRE_DIV1         0x0u
#define RCC_APBPRE_DIV2         0x4u
#define RCC_APBPRE_DIV4         0x5u

/* Enable bit positions, from the header's own _Pos defines. */
#define RCC_AHB4ENR_GPIOBEN_POS   1u
#define RCC_AHB4ENR_GPIOCEN_POS   2u
#define RCC_AHB4ENR_GPIODEN_POS   3u   /* the console's PD8 and PD9 */
#define RCC_AHB4ENR_GPIOEEN_POS   4u
#define RCC_APB1LENR_USART3EN_POS 18u

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
#define CONSOLE_USART_BASE 0x40004800u  /* CD_APB1PERIPH_BASE + 0x4800, USART3.
                                         * Settled. The pins below are not, and
                                         * uart.c needs both. */
/* Confirmed Friday 2 October 2026 from ST's own board support package for this
 * Nucleo, Drivers/BSP/STM32H7xx_Nucleo/stm32h7xx_nucleo.h in
 * STM32Cube_FW_H7_V1.13.0, which states
 *
 *   COM1_UART          USART3
 *   COM1_TX_PIN        GPIO_PIN_8      COM1_TX_GPIO_PORT  GPIOD
 *   COM1_RX_PIN        GPIO_PIN_9      COM1_RX_GPIO_PORT  GPIOD
 *   COM1_TX_AF         GPIO_AF7_USART3 COM1_RX_AF         GPIO_AF7_USART3
 *
 * These three lines used to say "believed" and "confirm in MB1363". The board
 * support package is the right authority for a board fact and it agrees with
 * what was believed, which is a pleasant outcome and not one that could be
 * assumed: the same file defines LED2 twice, on PE1 and on PB7, because it
 * serves several H7 Nucleo boards through conditional compilation. So a value
 * read out of it has to be read in its branch rather than grepped flat. Ours
 * is the PE1 branch, which is the LD2 yellow this volume has settled. */
#define CONSOLE_TX_PORT    GPIOD
#define CONSOLE_TX_PIN     8u    /* PD8, BSP COM1_TX_PIN */
#define CONSOLE_RX_PORT    GPIOD
#define CONSOLE_RX_PIN     9u    /* PD9, BSP COM1_RX_PIN */
#define CONSOLE_AF         7u    /* BSP GPIO_AF7_USART3 */

/* Defined Friday 2 October 2026. Both of the independent grounds uart.c refused
 * on are now answered: the pins by ST's board support package above, and the
 * baud rate divider by board_pclk1_hz(), which is decoded from the RCC registers
 * rather than guessed. */
#define BOARD_CONSOLE_PINS_CONFIRMED \
    "BSP stm32h7xx_nucleo.h + RCC decode, Friday 2 October 2026"

/* The console's own numbers, each with the source that settles it.
 *
 * 115200 is the rate the ST-LINK's virtual serial port is conventionally read
 * at, and it is a choice rather than a fact: the probe presents whatever the
 * target sends, so this and the terminal have to agree and nothing else checks
 * it. If the terminal shows rubbish, this is the first number to suspect, not
 * the divider below. */
#define CONSOLE_BAUD       115200u

/* USART register offsets, from USART_TypeDef's member order in ST's device
 * header: CR1 0x00, CR2 0x04, CR3 0x08, BRR 0x0C, GTPR 0x10, RTOR 0x14,
 * RQR 0x18, ISR 0x1C, ICR 0x20, RDR 0x24, TDR 0x28, PRESC 0x2C. */
#define USART_CR1          0x00u
#define USART_BRR          0x0Cu
#define USART_ISR          0x1Cu
#define USART_TDR          0x28u
#define USART_PRESC        0x2Cu
#define USART_REG(base, off) REG32((base) + (off))

/* Control and status bits, from the header's _Pos defines. */
#define USART_CR1_UE_POS           0u
#define USART_CR1_RE_POS           2u
#define USART_CR1_TE_POS           3u

/* Bit 7 of ISR, and the name is the point. The header calls it
 * USART_ISR_TXE_TXFNF, one flag serving two meanings depending on whether the
 * FIFO is enabled. Looking for a bit called TXE finds nothing on this
 * peripheral, which is a small trap with a quick cure and was worth ten minutes
 * once. The FIFO stays disabled here, its reset state, so this reads as plain
 * transmit-data-register-empty. */
#define USART_ISR_TXE_TXFNF_POS    7u

/* The baud rate divider, and a field name in the device header that must NOT be
 * believed.
 *
 * stm32h7a3xxq.h declares BRR as USART_BRR_DIV_MANTISSA at bits 4 to 15 and
 * USART_BRR_DIV_FRACTION at bits 0 to 3. Those names are inherited from the
 * older STM32 USART, where the divider really was a mantissa and a sixteenth
 * fraction. On this peripheral with oversampling by 16 the register holds the
 * divider as a plain integer, and ST's own HAL says so in one line:
 *
 *   #define UART_DIV_SAMPLING16(PCLK, BAUD, PRESC)
 *       ((((PCLK)/UARTPrescTable[(PRESC)]) + ((BAUD)/2U)) / (BAUD))
 *
 * A rounded integer division, nothing more. Computing (mantissa << 4) |
 * (fraction * 16) from those field names, which is what they invite, gives a
 * badly wrong rate from correctly cited defines. A name in a header is not a
 * specification.
 *
 * UARTPrescTable[12] = {1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256}, so index
 * 0 is a prescaler of 1, and PRESC resets to 0 and nothing here writes it.
 *
 * The kernel clock is selection 0 of RCC_CDCCIP2R, read live as 0x00000000, and
 * ST names that RCC_USART234578CLKSOURCE_CDPCLK1, aliased to
 * RCC_USART234578CLKSOURCE_PCLK1. So the clock is pclk1, which board_pclk1_hz()
 * reports. Worth noting that the same HAL header defines that constant twice in
 * two conditional branches, once against RCC_D2CCIP2R for the STM32H743 and once
 * against RCC_CDCCIP2R for this part. Different registers, same macro name. */
#define USART_BRR_FROM(pclk, baud)  (((pclk) + ((baud) / 2u)) / (baud))

/* The pull the button needs, and the defect it revealed. ST's board support
 * package initialises BUTTON_USER with
 *
 *   gpio_init_structure.Pull = GPIO_PULLDOWN;
 *
 * so MB1363 fits no resistor of its own and relies on the MCU's internal
 * pull-down to define the released level. Until Friday 2 October 2026 this
 * board support configured the three LEDs and did nothing whatever to PC13, so
 * the pin sat at its reset state of input with no pull and its released level
 * was undefined. PUPDR takes two bits per pin: 00 none, 01 pull-up, 10
 * pull-down. */
#define GPIO_PUPD_NONE      0u
#define GPIO_PUPD_PULLUP    1u
#define GPIO_PUPD_PULLDOWN  2u

/* The reset clock, and what changed about it on Friday 2 October 2026.
 *
 * This used to be HSI_HZ_UNCONFIRMED, with a comment saying that the family's
 * internal oscillator is commonly 64 MHz and that commonly is not a
 * measurement. That was right to refuse. It is no longer the situation.
 *
 * The running part was read over SWD and every link in the chain was then
 * confirmed against a named define in ST's device header:
 *
 *   RCC_CR      0x00004025   HSION, HSIRDY, HSIDIV = 00 which is
 *                            RCC_CR_HSIDIV_1, divide by one. HSEON = 0 and
 *                            PLL1ON = 0, so neither the external 8 MHz nor
 *                            the PLL is running.
 *   RCC_CFGR    0x00000000   SWS = 000 = RCC_CFGR_SWS_HSI, so HSI really is
 *                            the system clock and not merely requested.
 *   RCC_CDCFGR1 0x00000000   RCC_CDCFGR1_CDCPRE_DIV1 and HPRE_DIV1.
 *   RCC_CDCFGR2 0x00000000   RCC_CDCFGR2_CDPPRE1_DIV1.
 *
 * So the core, the AHB and the APB1 bus are all at hsi_ck, undivided.
 *
 * NOMINAL, not measured, and the word is chosen. 64 MHz is the datasheet figure
 * for this oscillator, carrying roughly one percent at room temperature and more
 * across temperature and supply. What was established is the configuration, to
 * certainty, and the frequency only to that tolerance. A 115200 baud divider has
 * ample margin for one percent. A microsecond figure in P06 now carries a stated
 * uncertainty instead of a refusal, which is a different claim and a weaker one
 * than a counted reference would give. */
/* MEASURED on Friday 2 October 2026 at 64.17 MHz, 0.27 per cent above this
 * nominal, and the nominal is what stays in the code. The reasoning for keeping
 * it matters as much as the measurement.
 *
 * How it was measured, with no instrument but a serial port. P01 prints a note
 * every twentieth blink cycle, so the note to note interval is twenty delays and
 * averages out host jitter. The delay loop runs a known number of iterations, and
 * the cycles per iteration must be a whole number because it is a fixed
 * instruction sequence. Measuring the interval therefore pins the product of
 * cycles and clock period, and the integer constraint separates them: 7 cycles
 * would imply 56.1 MHz and 9 would imply 72.1 MHz, so the count is 8 and what
 * remains is the frequency.
 *
 * Done twice, through two different code paths, 8,000,000 iterations per cycle
 * and then 4,000,000 after the divisor was corrected:
 *
 *   8,000,000 iterations   998.25 ms per cycle   64.18 MHz
 *   4,000,000 iterations   499.76 ms per cycle   64.16 MHz
 *
 * Those two first disagreed by 0.12 per cent, which was the useful part. The note
 * itself is 237 characters, 20.6 ms on the wire at 115200 and 1.03 ms amortised
 * over twenty cycles, and that fixed overhead weighs differently against a 998 ms
 * delay than against a 499 ms one. Subtracting it brings the two to agree within
 * 0.025 per cent. A disagreement whose shape predicts its own cause is better
 * evidence than two numbers that happened to match.
 *
 * CONFIRMED FOUR MORE TIMES, same day, after the cycle counter arrived. Pairing
 * an on-chip cycle count with the host's measurement of real time determines the
 * frequency without the integer argument above, and three successive refinements
 * of the delay calibration each produced a reading:
 *
 *   host clock plus an integer constraint        64.17 MHz
 *   host clock plus on-chip cycles               64.19 MHz
 *   again, after the two-point fix               64.18 MHz
 *   again, after the warm-up fix, three times    64.17, 64.17, 64.18 MHz
 *
 * Six reductions, two instruments, spread 0.031 per cent. The oscillator is
 * 64.17 to 64.18 MHz, about 0.27 per cent above nominal.
 *
 * WHY THE CODE KEEPS 64000000 ANYWAY. 64.17 MHz is this die, on this board, at
 * whatever the room was that afternoon, against a host PC's clock that is
 * traceable to nothing. The oscillator's spread across parts, temperature and
 * supply is wider than the 0.27 per cent found here, so substituting the measured
 * figure would fit the code to one chip on one afternoon and would read as more
 * precise while being less general. The nominal is the honest constant; the
 * measurement is evidence that the nominal is good to about three parts in a
 * thousand on this board, which is what a reader actually needs to know. */
#define HSI_HZ_NOMINAL     64000000u

/* The target. 280 MHz, reached the way ST reaches it on this exact board.
 *
 * THE FACTORISATION CHANGED ON SUNDAY 4 OCTOBER 2026 and the old one is worth
 * leaving here as a warning. This comment used to read "8 / 2 times 140 / 2
 * gives 280 MHz", which is arithmetically correct: an 8 MHz input divided by 2
 * is 4 MHz, times 140 is a 560 MHz oscillator, divided by 2 is 280 MHz.
 *
 * ST divides it differently. Its own example for the NUCLEO-H7A3ZI-Q uses
 * PLLM 4, PLLN 280, PLLP 2: a 2 MHz input, the same 560 MHz oscillator, the same
 * 280 MHz out. Both are right about the output and they are not interchangeable,
 * because the PLL input range field has to contain the input, and ST pairs its
 * choice with PLL1VCIRANGE_1, which is the 2 to 4 MHz range. Writing the old
 * factorisation while copying that range constant would have set a range that
 * does not contain 4 MHz, and nothing would have refused.
 *
 * So the numbers below are ST's, from the source named at
 * BOARD_CLOCK_280_SOURCE, and not an independent derivation that happens to
 * agree on the product.
 */
#define CORE_HZ_TARGET     280000000u

/* WHAT 280 MHz ACTUALLY MEASURES AS, AND IT MEASURES AS TWO DIFFERENT THINGS.
 * The constant above is the target of a configuration. These are observations,
 * and there are two because the second one disagreed with the first.
 *
 *   run 1   279672822 Hz   1168 ppm below nominal
 *   run 2   279435368 Hz   2017 ppm below nominal
 *
 * Both on Sunday 4 October 2026, both on this board, both the same image
 * measuring against this board's 32.768 kHz crystal over a one second gate, a
 * few hours apart with a rebuild and a reflash between them. They differ by 849
 * parts per million.
 *
 * SO A NINE DIGIT FIGURE HERE WAS NEVER SUPPORTABLE, and for a few hours this
 * file carried one as though it were. What reproduces is the sign and the order
 * of magnitude: the core runs below its nominal by one to two parts in a
 * thousand, and the cause is the input frequency and not this part. See
 * HSE_HZ_BYPASS.
 *
 * THE SECOND RUN EXCLUDES THE EASY EXPLANATION, which is why it is worth more
 * than the first. Both frequencies are gated by the same crystal, so a crystal
 * that drifted would move both readings by the same relative amount. The
 * internal oscillator moved 119 parts per million between the runs and the PLL
 * moved 849, so the external clock moved 730 parts per million RELATIVE to the
 * internal one. No drift of the shared reference can produce that. It is a
 * second observation of what the first run could only infer: whatever generates
 * the debugger's 8 MHz is not a crystal.
 *
 * HOW THE CRYSTAL WAS ITSELF CHECKED, since a measurement is only as good as its
 * reference. The same method was applied to the internal oscillator, which two
 * other instruments had already measured at 64.17 to 64.18 MHz across six
 * reductions. The crystal says 64194318 Hz, which agrees with them to between
 * 223 and 379 parts per million. The alternative explanation for the PLL result,
 * a crystal running 1171 parts per million fast, would have put that reading
 * near 64100000 Hz, so it is excluded by a factor of three.
 *
 * SO THE UNCERTAINTY IS A FEW HUNDRED PARTS PER MILLION AND NOT A FEW TENS.
 * Earlier comments in this repository said a 32.768 kHz crystal is good to a few
 * tens of parts per million, which is a datasheet expectation rather than
 * anything demonstrated here. What has been demonstrated is that the crystal and
 * a host PC's clock agree to a few hundred, and the crystal is probably the
 * better of the two, but this bench cannot show which is wrong. A few hundred
 * parts per million is enough to establish the sign of an effect of one to two
 * parts in a thousand, and not enough to quote its last digit. The 849 parts per
 * million between the two runs says the same thing from the other direction.
 *
 * NOTHING COMPUTES FROM EITHER OF THESE. They are recorded observations, named
 * for their runs rather than averaged, because the mean of two readings taken
 * under conditions nobody controlled is a third number with no better claim than
 * either of them. */
#define CORE_HZ_MEASURED_1 279672822u
#define CORE_HZ_MEASURED_2 279435368u

/* The high speed clock this board actually has: the on-board debugger drives it
 * in bypass mode, so there is no crystal to start and HSEBYP must be set before
 * HSEON or the part waits for an oscillator that is not fitted. A settled board
 * fact, from the same two machine-readable sources as the LED pins.
 *
 * AND IT IS NOT 8 MHz, AND IT IS NOT STEADY EITHER. Measured twice on Sunday
 * 4 October 2026 against this board's own 32.768 kHz crystal, a few hours apart:
 * about 7990652 Hz and then about 7983868 Hz, 1168 and 2017 parts per million
 * low, 849 parts per million apart from each other. The derivation is short
 * because the PLL's dividers are integers and its fractional term is off:
 * sys_ck is HSE times 280 over 4 times 2, so times 35, and the two measured
 * cores of 279672822 and 279435368 Hz put HSE at those two figures.
 *
 * THE MOVEMENT IS THE EVIDENCE, not a nuisance in it. Both clocks in a run are
 * gated by the same crystal, so a crystal that drifted between the runs would
 * move both readings by the same relative amount. The internal oscillator moved
 * 119 parts per million and the PLL moved 849, so this clock moved 730 parts per
 * million RELATIVE to the internal one. A crystal-derived 8 MHz does not do that
 * on a bench at room temperature in an afternoon; an RC oscillator does. Under
 * the rule that one observation is not a mechanism, that is the second
 * observation, and it agrees with what the first run could only infer.
 *
 * WHY THE CONSTANT STAYS 8000000 ANYWAY, which is the same argument this file
 * already makes for HSI_HZ_NOMINAL and which the second run strengthens. The
 * measured figure is this probe, on this board, at one moment, against a
 * reference whose own accuracy is not traceable, and the figure moved.
 * Substituting it would have fitted the code to one sample and read as more
 * precise while being less general. The nominal is the honest constant and the
 * measurements are evidence about how good the nominal is, which is what a
 * reader needs.
 *
 * THE CONSEQUENCE, AND ITS SIGN, because "0.117 per cent high" is what an earlier
 * version of this comment said and it is not precise enough. The two clocks are
 * wrong in OPPOSITE directions, so a derived figure flips sign between the two
 * images:
 *
 *                              reset clock        280 MHz setting
 *   board_core_hz() reports    64000000           280000000
 *   the truth, run 1           64194318           279672822
 *   the reported FREQUENCY     3027 ppm LOW       1170 ppm HIGH
 *   a measured DURATION        3036 ppm LONG      1168 ppm SHORT
 *   a requested DELAY          3027 ppm SHORT     1170 ppm LONG
 *
 * The figures are run 1's. Run 2 differs in magnitude and not in sign, which is
 * the only part of this table anybody should rely on, and clocktree_bias prints
 * all three for whichever run the board is having.
 *
 * Nothing in this repository quotes a time to better than a part in a thousand,
 * so nothing published is wrong today. What this table is for is the next figure
 * that wants to be. */
#define HSE_HZ_BYPASS      8000000u

/* ------------------------------------------------------------------ the 280 MHz
 * tree: the registers, the fields and where each came from.
 *
 * Confirmed Sunday 4 October 2026. The authority is the same one the rest of
 * this header names, ST's CMSIS device header for this exact die, read on the
 * win11 skyhorizon demo laptop, plus two files beside it for the parts a CMSIS
 * header does not carry. All three are named at each value below.
 *
 * A NOTE ON THE OFFSETS, because this is where the family trap bit hardest.
 * These are member positions inside RCC_TypeDef, PWR_TypeDef and FLASH_TypeDef,
 * computed from the declaration order, and NOT taken from the trailing comments
 * in those declarations. From RCC's RSR onward those comments are the
 * STM32H743's: they say RSR is at 0xD0 where the declaration order gives 0x130,
 * a difference of 0x60, because the H743's reserved gap before RSR is smaller.
 * That is why RCC_AHB4ENR above is 0x140 and not the 0xE0 the comment claims.
 * The three registers added here sit before that gap, at 0x28, 0x2C and 0x30,
 * where the comments and the declaration order agree, and they were still
 * counted rather than read.
 */
#define BOARD_CLOCK_280_SOURCE \
    "ST CMSIS stm32h7a3xxq.h for the fields; " \
    "Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_pwr.h for the voltage " \
    "scale encoding; Projects/NUCLEO-H7A3ZI-Q/Applications/EEPROM/" \
    "EEPROM_Emulation/Src/main.c for the dividers, the ranges and the flash " \
    "latency at 280 MHz. STM32Cube_FW_H7_V1.13.0, read Sunday 4 October 2026"

#define RCC_PLLCKSELR   REG32(RCC_BASE + 0x028u)  /* PLL source and DIVM */
#define RCC_PLLCFGR     REG32(RCC_BASE + 0x02Cu)  /* ranges and output enables */
#define RCC_PLL1DIVR    REG32(RCC_BASE + 0x030u)  /* N, P, Q, R for PLL1 */

#define PWR_SRDCR       REG32(PWR_BASE + 0x018u)  /* voltage scaling lives here */
#define FLASH_ACR       REG32(FLASH_BASE_REG + 0x000u)

/* HSE, from the header's own _Pos defines. The order matters at run time: bypass
 * before enable, then wait for ready. */
#define RCC_CR_HSEBYP_MSK       (1u << 18)
#define RCC_CR_HSERDY_MSK       (1u << 17)
#define RCC_CR_PLL1RDY_MSK      (1u << 25)

/* The PLL source field, and the one value this board uses. */
#define RCC_PLLCKSELR_PLLSRC_MSK    (3u << 0)
#define RCC_PLLSRC_HSE              2u      /* RCC_PLLCKSELR_PLLSRC_HSE */

/* DIVM1 IS WRITTEN AS THE VALUE ITSELF, and N1, P1, Q1 and R1 are written as the
 * value minus one. That asymmetry is not a guess and not a symmetry anybody
 * should assume: the HAL reads them back as
 *
 *     PLLM = (PLLCKSELR & DIVM1) >> DIVM1_Pos            no adjustment
 *     PLLN = ((PLL1DIVR & N1) >> N1_Pos) + 1             plus one
 *     PLLP = ((PLL1DIVR & P1) >> P1_Pos) + 1             plus one
 *
 * in stm32h7xx_hal_rcc.c, which is what settles it. Getting N1 wrong by one is
 * 279 MHz instead of 280, which is 0.36 per cent: too small to notice by eye and
 * too large to ignore in a timing figure. */
#define RCC_PLLCKSELR_DIVM1_POS     4u
#define RCC_PLLCKSELR_DIVM1_MSK     (0x3Fu << RCC_PLLCKSELR_DIVM1_POS)
#define RCC_PLL1DIVR_N1_POS         0u
#define RCC_PLL1DIVR_N1_MSK         (0x1FFu << RCC_PLL1DIVR_N1_POS)
#define RCC_PLL1DIVR_P1_POS         9u
#define RCC_PLL1DIVR_P1_MSK         (0x7Fu << RCC_PLL1DIVR_P1_POS)
#define RCC_PLL1DIVR_Q1_POS         16u
#define RCC_PLL1DIVR_Q1_MSK         (0x7Fu << RCC_PLL1DIVR_Q1_POS)
#define RCC_PLL1DIVR_R1_POS         24u
#define RCC_PLL1DIVR_R1_MSK         (0x7Fu << RCC_PLL1DIVR_R1_POS)

/* The oscillator range and the input range. VCOSEL 0 is the wide range, which is
 * what ST selects for the 560 MHz oscillator this tree runs. The input range
 * field is two bits and the HAL names all four: 1 to 2, 2 to 4, 4 to 8 and 8 to
 * 16 MHz at field values 0 to 3. This tree feeds the PLL 2 MHz, so it takes
 * value 1, which is what ST's example writes. */
#define RCC_PLLCFGR_PLL1FRACEN_MSK  (1u << 0)
#define RCC_PLLCFGR_PLL1VCOSEL_MSK  (1u << 1)
#define RCC_PLLCFGR_PLL1RGE_POS     2u
#define RCC_PLLCFGR_PLL1RGE_MSK     (3u << RCC_PLLCFGR_PLL1RGE_POS)
#define RCC_PLL1VCO_WIDE            0u      /* RCC_PLL1VCOWIDE */
#define RCC_PLL1VCI_2_TO_4_MHZ      1u      /* RCC_PLL1VCIRANGE_1 */
#define RCC_PLLCFGR_DIVP1EN_MSK     (1u << 16)

/* The system clock switch. SW selects, SWS reports, and the two have different
 * positions, which is why both are here. */
#define RCC_CFGR_SW_POS             0u
#define RCC_CFGR_SW_MSK             (7u << RCC_CFGR_SW_POS)
#define RCC_SW_PLL1                 3u      /* RCC_CFGR_SW_PLL1 */

/* THE VOLTAGE SCALING, AND THE SHARPEST INSTANCE OF THIS VOLUME'S TRAP.
 *
 * VOS is two bits at 15:14 of PWR_SRDCR, with VOSRDY at 13. The encoding below
 * is from stm32h7xx_hal_pwr.h, where it reads
 *
 *     #define PWR_REGULATOR_VOLTAGE_SCALE0  (PWR_SRDCR_VOS_1 | PWR_SRDCR_VOS_0)
 *     #define PWR_REGULATOR_VOLTAGE_SCALE3  (0U)
 *
 * In the same file, guarded for the STM32H743, the same two names read
 *
 *     #define PWR_REGULATOR_VOLTAGE_SCALE0  (0U)
 *     #define PWR_REGULATOR_VOLTAGE_SCALE3  (PWR_D3CR_VOS_0)
 *
 * The names are identical and the encodings are reversed. Scale 0 is the highest
 * performance on both parts and it is 0b11 here and 0b00 there. Writing this
 * field from STM32H743 knowledge would select the LOWEST performance scale while
 * believing it had selected the highest, and would then run 280 MHz with six
 * wait states outside the regulator's range. Nothing refuses; it is simply
 * wrong, and it would be wrong intermittently, which is worse. */
#define PWR_SRDCR_VOS_POS       14u
#define PWR_SRDCR_VOS_MSK       (3u << PWR_SRDCR_VOS_POS)
#define PWR_SRDCR_VOSRDY_MSK    (1u << 13)
#define PWR_VOS_SCALE3          0u      /* the reset scale, lowest performance */
#define PWR_VOS_SCALE1          2u      /* the only way to reach scale 0 */
#define PWR_VOS_SCALE0          3u      /* highest performance ON THIS PART */

/* SCALE 0 IS ONLY REACHABLE FROM SCALE 1, and this was learned from the board
 * on Sunday 4 October 2026 rather than from the register description.
 *
 * The first attempt wrote scale 0 straight over the reset value, which is scale
 * 3, and read it back correctly: VOS held 3, so the encoding above was right.
 * VOSRDY never set, for a million polls, which is tens of milliseconds against a
 * flag that settles in microseconds. Nothing in the field description says why.
 *
 * The answer is in the doc comment above __HAL_PWR_VOLTAGESCALING_CONFIG in
 * stm32h7xx_hal_pwr.h: "Transition to Voltage Scale 0 is only possible when the
 * system is already in Voltage Scale 1." It is a constraint on the transition
 * and not on the value, so no amount of reading the VOS field would have
 * revealed it, and the part does not report it as an error: it simply declines
 * to become ready.
 *
 * WHAT THAT SAYS ABOUT ST'S OWN EXAMPLE, which is the uncomfortable part. The
 * NUCLEO-H7A3ZI-Q example this file took its dividers from writes scale 0
 * directly from reset and then waits with while(!VOSRDY){}. On this board that
 * bit does not set, so that loop does not terminate. The bounded wait in
 * clock280.c is the only reason this came back as a diagnosis rather than as an
 * image that stopped.
 */

/* THE SUPPLY CONFIGURATION, which has to happen before any voltage scaling and
 * which this repository did not know about until the board refused twice.
 *
 * At reset this part is in what ST's own comment calls Run* mode: both SMPSEN
 * and LDOEN are set in PWR_CR3, which is not a supply selection but the absence
 * of one. In that state the regulator will not change voltage scale. It does not
 * report an error; VOSRDY simply never sets, and PWR_CSR1's ACTVOSRDY is clear
 * from reset, which is the bit that says so.
 *
 * Exiting Run* mode is a single write selecting one supply, and it LOCKS until
 * the next reset. ST's per-board system_stm32h7xx.c does it, for the LDO on an
 * SMPS part, as
 *
 *     PWR->CR3 = (PWR->CR3 & ~PWR_CR3_SMPSEN) | PWR_CR3_LDOEN;
 *     while ((PWR->CSR1 & PWR_CSR1_ACTVOSRDY) == 0U) {}
 *
 * THE SMPS AND NOT THE LDO ON THIS BOARD, from two independent sources that
 * agree. Every CubeIDE project file under Projects/NUCLEO-H7A3ZI-Q defines
 * USE_PWR_DIRECT_SMPS_SUPPLY, and that branch of ST's ExitRun0Mode is
 *
 *     PWR->CR3 &= ~(PWR_CR3_LDOEN);
 *
 * which clears the LDO and leaves the SMPS running. ST sets the macro in the
 * project configuration and not in any header, which is why searching the
 * headers for it finds nothing.
 *
 * THE SECOND SOURCE IS THE BOARD, and it is the only evidence here that comes
 * from hardware rather than from a file. On Sunday 4 October 2026 this file
 * briefly did the opposite, clearing SMPSEN and setting LDOEN, on the argument
 * that the LDO is on the die and therefore the safe choice. The board printed
 * its banner, reached that write, and stopped: no step report, no reset loop, no
 * further output. A power cycle recovered it completely and p01-first-light ran
 * unchanged afterwards. Nothing was damaged, and the reading is that this
 * board's core is supplied through the SMPS, so removing the SMPS removed the
 * supply.
 *
 * The argument that was wrong is worth keeping next to the answer. "The LDO is
 * on the die, so selecting it cannot hurt" is true about the die and says
 * nothing about the board, and a supply is a board fact. Reasoning about which
 * way a risk points is not a substitute for reading what the board is wired to
 * do, and this repository's own rule already said so.
 *
 * WHERE THIS WAS FOUND, because the first place looked was the wrong one. There
 * are two files named system_stm32h7xx.c in the pack. The CMSIS template under
 * Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates has no supply
 * configuration at all. A per-project copy in each of the ninety examples under
 * Projects/NUCLEO-H7A3ZI-Q does, and that is the one that matters. Reading only
 * the template supports the conclusion that ST never configures the supply,
 * which is wrong.
 */
#define PWR_CR3                 REG32(PWR_BASE + 0x00Cu)
#define PWR_CR3_BYPASS_MSK      (1u << 0)
#define PWR_CR3_LDOEN_MSK       (1u << 1)
#define PWR_CR3_SMPSEN_MSK      (1u << 2)

/* PWR_CSR1, which reports the scale ACTUALLY IN USE as opposed to the one
 * selected in SRDCR. Worth having in the dump for the same reason the read-back
 * is worth having: a selected value and an active value that disagree is a
 * different fault from a write that did not land, and ACTVOS is the only field
 * that can tell them apart. Offset 0x04, from the declaration order of
 * PWR_TypeDef. */
#define PWR_CSR1                REG32(PWR_BASE + 0x004u)
#define PWR_CSR1_ACTVOS_POS     14u
#define PWR_CSR1_ACTVOS_MSK     (3u << PWR_CSR1_ACTVOS_POS)
#define PWR_CSR1_ACTVOSRDY_MSK  (1u << 13)

/* The flash access latency. The field is four bits at 3:0 and ST's own example
 * for this board passes FLASH_LATENCY_6 at 280 MHz, which is the value 6. The
 * number of wait states a frequency needs is a datasheet table keyed on the
 * voltage scale and the bus clock, and it is NOT in any header, so this is the
 * one value here that rests on an example rather than on a definition. That is
 * named rather than hidden, and the read-back gate is what makes it safe to act
 * on: too many wait states is slow and correct, too few is garbage. */
#define FLASH_ACR_LATENCY_POS   0u
#define FLASH_ACR_LATENCY_MSK   (0xFu << FLASH_ACR_LATENCY_POS)
#define FLASH_LATENCY_280MHZ    6u

/* WRHIGHFREQ at 5:4 is deliberately left at its reset value. It sets the
 * programming delay rather than the read latency, ST's clock configuration never
 * writes it, and no image in this repository programs flash at 280 MHz. When one
 * does, this is the field to read about first. */

/* ---------------------------------------------- the 32.768 kHz crystal
 *
 * Why this is here at all. Every frequency in this volume is DERIVED: an 8 MHz
 * board fact multiplied and divided by fields read back out of registers. The
 * read-back confirms the bits and not their meaning, and that is the residual
 * doubt. Reading DIVM1 as 4 does not prove the field is a plain divisor.
 *
 * The 32.768 kHz crystal is the one reference on this board that does not come
 * from the PLL chain, so it is what can settle the interpretation rather than
 * the bits. A crystal at that frequency is good to a few tens of parts per
 * million, which is two orders better than the 0.36 per cent separating 280 MHz
 * from 279, and it needs no instrument and no wiring. That it is fitted is a
 * settled board fact from the same two machine-readable sources as the LED pins,
 * and until Sunday 4 October 2026 nothing in this repository had ever asked it
 * to oscillate.
 *
 * THE BACKUP DOMAIN IS WRITE PROTECTED AT RESET, and that is the trap to expect
 * here: RCC_BDCR ignores writes while PWR_CR1's DBP bit is clear, and ignoring a
 * write is not an error. The same shape as the supply configuration earlier
 * today, which declined in silence until a prerequisite was met, so this one is
 * read back and reported rather than assumed.
 *
 * AND THE BACKUP DOMAIN SURVIVES A SYSTEM RESET, like the supply selection does.
 * So the crystal may already be running when an image starts, and the honest
 * reading of a zero startup time is "it was already on", which is why the record
 * carries BDCR as found before anything is written.
 *
 * Confirmed Sunday 4 October 2026 from the same ST CMSIS header as the rest.
 */
#define PWR_CR1                 REG32(PWR_BASE + 0x000u)
#define PWR_CR1_DBP_MSK         (1u << 8)   /* clear = the backup domain is read only */

#define RCC_BDCR                REG32(RCC_BASE + 0x070u)
#define RCC_BDCR_LSEON_MSK      (1u << 0)
#define RCC_BDCR_LSERDY_MSK     (1u << 1)
#define RCC_BDCR_LSEBYP_MSK     (1u << 2)   /* an external clock, NOT this board */
#define RCC_BDCR_LSEDRV_POS     3u
#define RCC_BDCR_LSEDRV_MSK     (3u << RCC_BDCR_LSEDRV_POS)
#define RCC_BDCR_RTCSEL_POS     8u
#define RCC_BDCR_RTCSEL_MSK     (3u << RCC_BDCR_RTCSEL_POS)
#define RCC_BDCR_RTCEN_MSK      (1u << 15)

/* The nominal, which is what a measurement is compared against rather than
 * derived from. Not measured here: the crystal's own error is the floor on any
 * figure this reference produces, and the datasheet tolerance is what that
 * floor rests on until somebody compares it with a better clock. */
#define LSE_HZ_NOMINAL          32768u

/* ------------------------------- the real-time clock, as a counter of crystal
 * ticks and nothing else
 *
 * Why the RTC rather than a timer. The crystal has to be OBSERVED for a cycle
 * count to be gated on it, and the two routes are a timer with the crystal as an
 * input capture source, which needs the timer's input selection, capture,
 * compare and control registers, or the RTC's sub second register, which needs
 * the clock selected and then only reads. The second is five register groups
 * fewer and writes nothing inside the RTC at all, so no write protection key is
 * needed and nothing in the backup domain is reconfigured beyond selecting a
 * clock for it.
 *
 * THE ADDRESS IS DERIVED AND THE DERIVATION IS ANCHORED, not recalled. The CMSIS
 * header gives RTC_BASE as SRD_APB4PERIPH_BASE + 0x4000, and
 * SRD_APB4PERIPH_BASE as PERIPH_BASE + 0x18000000. PERIPH_BASE is pinned by two
 * values already in this file: SRD_AHB4PERIPH_BASE is PERIPH_BASE + 0x18020000,
 * and RCC_BASE of 0x58024400 and PWR_BASE of 0x58024800 are that base plus
 * 0x4400 and 0x4800. So PERIPH_BASE is 0x40000000, SRD_APB4PERIPH_BASE is
 * 0x58000000, and the RTC is at 0x58004000.
 *
 * THE FAMILY TRAP IS HERE TOO, and this is where it would have been invisible.
 * This part has RTC_ICSR at offset 0x0C with the registers-synchronised flag at
 * bit 5. The STM32H743 has RTC_ISR in that position with a different layout, and
 * almost all STM32H7 material is written against the H743. Offsets below are
 * member positions in RTC_TypeDef, counted from its declaration order.
 *
 * AND THE REGISTERS READ AS ZERO UNTIL THEIR BUS CLOCK IS ON. RCC_APB4ENR's
 * RTCAPBEN gates access to every register below, and without it they read zero
 * rather than refusing, which is the same failure shape as the backup domain's
 * write protection and as the supply configuration. It is enabled and read back.
 *
 * Confirmed Sunday 4 October 2026 from the same ST CMSIS header as the rest.
 */
#define RCC_APB4ENR             REG32(RCC_BASE + 0x154u)
#define RCC_APB4ENR_RTCAPBEN_MSK (1u << 16)
#define RCC_RTCSEL_LSE          1u      /* RCC_BDCR_RTCSEL_0 */

#define RTC_BASE_ADDR           0x58004000u
#define RTC_SSR                 REG32(RTC_BASE_ADDR + 0x008u)
#define RTC_ICSR                REG32(RTC_BASE_ADDR + 0x00Cu)
#define RTC_PRER                REG32(RTC_BASE_ADDR + 0x010u)

#define RTC_ICSR_RSF_MSK        (1u << 5)   /* the shadow registers are valid */
#define RTC_PRER_PREDIV_A_POS   16u
#define RTC_PRER_PREDIV_A_MSK   (0x7Fu << RTC_PRER_PREDIV_A_POS)
#define RTC_SSR_SS_MSK          0xFFFFu

/* The sub second register counts DOWN at the crystal divided by PREDIV_A plus
 * one, which with the reset value of 127 is 256 Hz. PREDIV_A is READ rather than
 * assumed, because assuming it is how a gate ends up wrong by a factor while
 * every other number looks right. */

#endif /* STM32H7A3_REGS_H */
