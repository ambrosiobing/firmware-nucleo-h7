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

/* The target, which is settled arithmetic from settled facts: the debugger
 * supplies 8 MHz in bypass mode, and 8 / 2 times 140 / 2 gives 280 MHz. What is
 * not settled is the register sequence that achieves it. */
#define CORE_HZ_TARGET     280000000u

#endif /* STM32H7A3_REGS_H */
