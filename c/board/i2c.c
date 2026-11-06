/* c/board/i2c.c: I2C1 as a controller on PB8 and PB9. The header carries the
 * sourcing for every pin and register fact; this file carries the sequence.
 *
 * NOTHING HERE POLLS WITHOUT A BOUND. Every wait is a counted loop that returns
 * I2C_ERR_TIMEOUT rather than spinning, because the one failure this bus
 * actually produces on a bench is a line held low by a device that was reset
 * mid-transfer, and an unbounded wait turns that into a board that says
 * nothing at all.
 */
#include "i2c.h"

#include "board.h"
#include "stm32h7a3_regs.h"

const char *i2c_status_name(i2c_status_t s)
{
    switch (s) {
    case I2C_OK:                  return "ok";
    case I2C_ERR_NOT_INITIALISED: return "not initialised";
    case I2C_ERR_TIMING:          return "timing";
    case I2C_ERR_PERIPHERAL:      return "peripheral";
    case I2C_ERR_BUS_STUCK:       return "bus stuck";
    case I2C_ERR_NACK:            return "nack";
    case I2C_ERR_ARBITRATION:     return "arbitration";
    case I2C_ERR_TIMEOUT:         return "timeout";
    }
    return "unknown";
}

#if defined(BOARD_REGS_CONFIRMED)

/* I2C1, RM0455 Rev 13 page 137: 0x40005400 to 0x400057FF. The offsets are
 * Table 391 on page 1938. */
#define I2C1_BASE       0x40005400u
#define I2C_REG(off)    REG32(I2C1_BASE + (off))

#define I2C_CR1         0x00u
#define I2C_CR2         0x04u
#define I2C_TIMINGR     0x10u
#define I2C_ISR         0x18u
#define I2C_ICR         0x1Cu
#define I2C_RXDR        0x24u
#define I2C_TXDR        0x28u

/* CR1, Table 391: PE is bit 0. */
#define I2C_CR1_PE      (1u << 0)

/* CR2, read off Table 391 from bit 31 down: five reserved, PECBYTE 26,
 * AUTOEND 25, RELOAD 24, NBYTES 23:16, NACK 15, STOP 14, START 13, HEAD10R 12,
 * ADD10 11, RD_WRN 10, SADD 9:0. */
#define I2C_CR2_SADD_POS    0u
#define I2C_CR2_RD_WRN      (1u << 10)
#define I2C_CR2_START       (1u << 13)
#define I2C_CR2_STOP        (1u << 14)
#define I2C_CR2_NBYTES_POS  16u
#define I2C_CR2_AUTOEND     (1u << 25)

/* ISR, same table: TXE 0, TXIS 1, RXNE 2, ADDR 3, NACKF 4, STOPF 5, TC 6,
 * TCR 7, BERR 8, ARLO 9, OVR 10, BUSY 15. */
#define I2C_ISR_TXE     (1u << 0)
#define I2C_ISR_TXIS    (1u << 1)
#define I2C_ISR_RXNE    (1u << 2)
#define I2C_ISR_NACKF   (1u << 4)
#define I2C_ISR_STOPF   (1u << 5)
#define I2C_ISR_TC      (1u << 6)
#define I2C_ISR_BERR    (1u << 8)
#define I2C_ISR_ARLO    (1u << 9)
#define I2C_ISR_BUSY    (1u << 15)

/* ICR: NACKCF 4, STOPCF 5, BERRCF 8, ARLOCF 9. Write one to clear. */
#define I2C_ICR_NACKCF  (1u << 4)
#define I2C_ICR_STOPCF  (1u << 5)
#define I2C_ICR_BERRCF  (1u << 8)
#define I2C_ICR_ARLOCF  (1u << 9)

/* RCC_APB1LENR bit 21, RM0455 page 454: "Bit 21 I2C1EN: I2C1 peripheral clocks
 * enable". Page 454 also says the peripheral takes TWO clocks, the kernel clock
 * selected by I2C123SEL and the rcc_pclk1 bus interface clock, which is why
 * i2c_init takes the kernel rate as an argument instead of deriving it. */
#define RCC_APB1LENR_I2C1EN_POS  21u

#define I2C_SCL_PORT    GPIOB
#define I2C_SCL_PIN     8u
#define I2C_SDA_PORT    GPIOB
#define I2C_SDA_PIN     9u
#define I2C_AF          4u      /* stm32duino variant, GPIO_AF4_I2C1 */

/* The bound on every poll. At 280 MHz this is a few milliseconds, which is
 * orders of magnitude longer than any byte on a 100 kHz bus and still short
 * enough that a stuck line is reported rather than waited on. */
#define I2C_POLL_LIMIT  200000u

static bool g_ready;

/* One pad into open-drain alternate function mode, in the order that never
 * leaves it driving: the function first, then the output type and the pull,
 * and MODER last, so the pad does not become an output before it knows what
 * drives it. Chapter 6 settled that order for the marker pin and the reason is
 * the same here. board_pin_alternate is not used because it is push-pull by
 * contract, and a push-pull SDA fights every device that pulls it low. */
static void i2c_pad(uint32_t port, uint32_t pin)
{
    const uint32_t pos2 = pin * 2u;
    const uint32_t pos4 = (pin % 8u) * 4u;
    const uint32_t afr  = (pin < 8u) ? GPIO_AFRL : GPIO_AFRH;

    GPIO_REG(port, afr) = (GPIO_REG(port, afr) & ~(0xFu << pos4))
                        | (I2C_AF << pos4);
    GPIO_REG(port, GPIO_OTYPER) |= (1u << pin);          /* open drain */
    GPIO_REG(port, GPIO_PUPDR) = (GPIO_REG(port, GPIO_PUPDR) & ~(3u << pos2))
                               | (GPIO_PUPD_NONE << pos2);
    GPIO_REG(port, GPIO_MODER) = (GPIO_REG(port, GPIO_MODER) & ~(3u << pos2))
                               | (2u << pos2);           /* alternate function */
}

static bool i2c_pad_ok(uint32_t port, uint32_t pin)
{
    const uint32_t pos2 = pin * 2u;
    const uint32_t pos4 = (pin % 8u) * 4u;
    const uint32_t afr  = (pin < 8u) ? GPIO_AFRL : GPIO_AFRH;
    const bool mode_ok = (GPIO_REG(port, GPIO_MODER) & (3u << pos2)) == (2u << pos2);
    const bool func_ok = (GPIO_REG(port, afr) & (0xFu << pos4)) == (I2C_AF << pos4);
    const bool type_ok = (GPIO_REG(port, GPIO_OTYPER) & (1u << pin)) != 0u;
    return mode_ok && func_ok && type_ok;
}

/* The timing fields, computed from RM0455 page 1931 rather than copied from
 * another part's example, because this manual gives no example table for this
 * part and a value lifted from an H743 project would be a guess wearing a
 * citation.
 *
 *     tPRESC  = (PRESC  + 1) x tI2CCLK
 *     tSCLL   = (SCLL   + 1) x tPRESC
 *     tSCLH   = (SCLH   + 1) x tPRESC
 *     tSCLDEL = (SCLDEL + 1) x tPRESC
 *     tSDADEL =  SDADEL      x tPRESC
 *
 * One SCL period is tSCLL + tSCLH plus the rise and fall the wire adds, which
 * this does not model: the achieved rate reported back is therefore an UPPER
 * bound on what a scope would measure, and it is reported rather than claimed
 * so that the first capture can correct it.
 *
 * The split is deliberately not even. The low half is made the longer one,
 * which is what every published setting for this peripheral does, because the
 * rise is slower than the fall on an open-drain bus with a pull-up. */
static i2c_status_t i2c_timing(uint32_t kernel_hz, uint32_t target_hz,
                               i2c_timing_t *out)
{
    if (kernel_hz == 0u || target_hz == 0u) {
        return I2C_ERR_TIMING;
    }

    for (uint32_t presc = 0u; presc < 16u; presc++) {
        const uint32_t presc_hz = kernel_hz / (presc + 1u);
        const uint32_t ticks    = presc_hz / target_hz;   /* per SCL period */

        /* Two counters of eight bits each, so the period must fit in 512
         * prescaled ticks, and it must be long enough for the halves to differ
         * at all. */
        if (ticks < 4u || ticks > 512u) {
            continue;
        }

        const uint32_t low  = (ticks * 9u) / 16u;         /* the longer half */
        const uint32_t high = ticks - low;
        if (low == 0u || high == 0u || low > 256u || high > 256u) {
            continue;
        }

        out->kernel_hz   = kernel_hz;
        out->target_hz   = target_hz;
        out->presc       = (uint8_t) presc;
        out->scll        = (uint8_t) (low - 1u);
        out->sclh        = (uint8_t) (high - 1u);
        /* One prescaled tick of hold and one of setup, the smallest values
         * that are not zero. SDADEL is allowed to be zero by the register and
         * is not used so here: a hold time of zero relies on the device's own
         * input filtering, which is not a property this board can check. */
        out->sdadel      = 1u;
        out->scldel      = 1u;
        out->achieved_hz = presc_hz / (low + high);
        out->timingr = ((uint32_t) out->presc  << 28)
                     | ((uint32_t) out->scldel << 20)
                     | ((uint32_t) out->sdadel << 16)
                     | ((uint32_t) out->sclh   <<  8)
                     | ((uint32_t) out->scll   <<  0);
        return I2C_OK;
    }
    return I2C_ERR_TIMING;
}

i2c_status_t i2c_init(uint32_t kernel_hz, uint32_t target_hz, i2c_timing_t *out)
{
    g_ready = false;
    if (out == 0) {
        return I2C_ERR_TIMING;
    }

    const i2c_status_t t = i2c_timing(kernel_hz, target_hz, out);
    if (t != I2C_OK) {
        return t;
    }

    RCC_AHB4ENR  |= (1u << RCC_AHB4ENR_GPIOBEN_POS);
    (void) RCC_AHB4ENR;                 /* read back: the write is posted */
    RCC_APB1LENR |= (1u << RCC_APB1LENR_I2C1EN_POS);
    (void) RCC_APB1LENR;

    /* PE cleared before TIMINGR, because that register is only writable while
     * the peripheral is disabled. */
    I2C_REG(I2C_CR1) &= ~I2C_CR1_PE;
    I2C_REG(I2C_TIMINGR) = out->timingr;
    out->timingr_read = I2C_REG(I2C_TIMINGR);
    if (out->timingr_read != out->timingr) {
        return I2C_ERR_PERIPHERAL;
    }

    i2c_pad(I2C_SCL_PORT, I2C_SCL_PIN);
    i2c_pad(I2C_SDA_PORT, I2C_SDA_PIN);
    if (!i2c_pad_ok(I2C_SCL_PORT, I2C_SCL_PIN)
        || !i2c_pad_ok(I2C_SDA_PORT, I2C_SDA_PIN)) {
        return I2C_ERR_PERIPHERAL;
    }

    I2C_REG(I2C_CR1) |= I2C_CR1_PE;
    if ((I2C_REG(I2C_CR1) & I2C_CR1_PE) == 0u) {
        /* The same read-back uart.c does on USART3's UE, and for the same
         * reason: a peripheral whose clock is not running accepts every write
         * and reads back zero. */
        return I2C_ERR_PERIPHERAL;
    }

    g_ready = true;
    return I2C_OK;
}

/* Wait for any of `mask`, or for a refusal the bus itself reports. */
static i2c_status_t i2c_wait(uint32_t mask, uint32_t *isr_out)
{
    for (uint32_t n = 0u; n < I2C_POLL_LIMIT; n++) {
        const uint32_t isr = I2C_REG(I2C_ISR);
        if ((isr & (I2C_ISR_BERR | I2C_ISR_ARLO)) != 0u) {
            I2C_REG(I2C_ICR) = I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
            return I2C_ERR_ARBITRATION;
        }
        if ((isr & I2C_ISR_NACKF) != 0u) {
            I2C_REG(I2C_ICR) = I2C_ICR_NACKCF;
            return I2C_ERR_NACK;
        }
        if ((isr & mask) != 0u) {
            if (isr_out != 0) {
                *isr_out = isr;
            }
            return I2C_OK;
        }
    }
    return I2C_ERR_TIMEOUT;
}

static i2c_status_t i2c_idle(void)
{
    for (uint32_t n = 0u; n < I2C_POLL_LIMIT; n++) {
        if ((I2C_REG(I2C_ISR) & I2C_ISR_BUSY) == 0u) {
            return I2C_OK;
        }
    }
    return I2C_ERR_BUS_STUCK;
}

/* Start a transfer: nbytes, direction, automatic stop. */
static void i2c_start(uint8_t addr7, uint32_t nbytes, bool read)
{
    uint32_t cr2 = ((uint32_t) addr7 << 1) << I2C_CR2_SADD_POS;
    cr2 |= (nbytes << I2C_CR2_NBYTES_POS) | I2C_CR2_AUTOEND | I2C_CR2_START;
    if (read) {
        cr2 |= I2C_CR2_RD_WRN;
    }
    I2C_REG(I2C_CR2) = cr2;
}

i2c_status_t i2c_probe(uint8_t addr7)
{
    if (!g_ready) {
        return I2C_ERR_NOT_INITIALISED;
    }
    const i2c_status_t idle = i2c_idle();
    if (idle != I2C_OK) {
        return idle;
    }

    /* A write of zero bytes. The address goes out and the automatic stop
     * follows the acknowledgement, so STOPF is the whole answer. */
    i2c_start(addr7, 0u, false);
    const i2c_status_t s = i2c_wait(I2C_ISR_STOPF, 0);
    I2C_REG(I2C_ICR) = I2C_ICR_STOPCF;
    return s;
}

i2c_status_t i2c_read_reg(uint8_t addr7, uint8_t reg, uint8_t *value)
{
    if (!g_ready) {
        return I2C_ERR_NOT_INITIALISED;
    }
    if (value == 0) {
        return I2C_ERR_NOT_INITIALISED;
    }
    const i2c_status_t idle = i2c_idle();
    if (idle != I2C_OK) {
        return idle;
    }

    /* One byte out, the register index, with no automatic stop so the read can
     * take the bus with a repeated start. */
    I2C_REG(I2C_CR2) = (((uint32_t) addr7 << 1) << I2C_CR2_SADD_POS)
                     | (1u << I2C_CR2_NBYTES_POS)
                     | I2C_CR2_START;
    i2c_status_t s = i2c_wait(I2C_ISR_TXIS, 0);
    if (s != I2C_OK) {
        return s;
    }
    I2C_REG(I2C_TXDR) = reg;

    s = i2c_wait(I2C_ISR_TC, 0);
    if (s != I2C_OK) {
        return s;
    }

    /* The repeated start, one byte in, automatic stop. */
    i2c_start(addr7, 1u, true);
    s = i2c_wait(I2C_ISR_RXNE, 0);
    if (s != I2C_OK) {
        return s;
    }
    *value = (uint8_t) (I2C_REG(I2C_RXDR) & 0xFFu);

    s = i2c_wait(I2C_ISR_STOPF, 0);
    I2C_REG(I2C_ICR) = I2C_ICR_STOPCF;
    return s;
}

#else  /* the register addresses are not confirmed */

i2c_status_t i2c_init(uint32_t kernel_hz, uint32_t target_hz, i2c_timing_t *out)
{
    (void) kernel_hz;
    (void) target_hz;
    (void) out;
    return I2C_ERR_NOT_INITIALISED;
}

i2c_status_t i2c_probe(uint8_t addr7)
{
    (void) addr7;
    return I2C_ERR_NOT_INITIALISED;
}

i2c_status_t i2c_read_reg(uint8_t addr7, uint8_t reg, uint8_t *value)
{
    (void) addr7;
    (void) reg;
    (void) value;
    return I2C_ERR_NOT_INITIALISED;
}

#endif
