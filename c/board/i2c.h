/* c/board/i2c.h: I2C1 as a controller, for the sensor shields.
 *
 * WHAT THIS IS FOR. P13 and P14 both put an X-NUCLEO sensor shield on the
 * Arduino header and talk to it over I2C1. This is the board layer under both,
 * and it is deliberately the smallest thing that can carry an identification
 * roll call: bring the peripheral up, see whether an address answers, and read
 * one register from it. The FIFO work in P13 and the dual-range work in P14
 * build on top; neither is here.
 *
 * THE PINS, read from the board manual on Friday 6 November 2026 and not from
 * convention. UM2408 Rev 6, Table 18 on page 44: connector CN7 pin 2 is Arduino
 * D15, signal I2C_A_SCL, STM32H7 pin PB8, function I2C1_SCL; CN7 pin 4 is
 * Arduino D14, signal I2C_A_SDA, STM32H7 pin PB9, function I2C1_SDA.
 *
 * THE ALTERNATE FUNCTION IS 4, AND IT RESTS ON ONE SOURCE, which is why it is
 * said out loud here. UM2408 names the pins and the peripheral and does NOT
 * give alternate-function numbers; those live in the part datasheet, which this
 * laptop does not hold. The number comes from stm32duino's variant for this
 * exact part, H7A3Z(G-I)TxQ_H7B3ZITxQ, whose PeripheralPins.c maps PB_8 and
 * PB_9 to I2C1 with GPIO_AF4_I2C1, in mode AF_OD with no pull. Zephyr would be
 * the second independent check and its H7 pinctrl set covers H723 through H757
 * with no H7A3 file, so there is no second source yet.
 *
 * A WRONG ALTERNATE FUNCTION CANNOT PASS SILENTLY HERE, which is what makes one
 * source tolerable: the pads would drive nothing, no address would acknowledge,
 * and i2c_roll_call refuses on an empty bus rather than reporting zero devices
 * as a result. That is the same shape as chapter 6's marker pad, where the
 * read-back of MODER and AFR was the only thing able to catch a pin that never
 * reached its timer channel.
 *
 * OPEN DRAIN AND NO INTERNAL PULL, also from that variant file. The shield
 * carries the bus pull-ups. A build that enables the internal pull-ups instead
 * would work on a bench and fail on a longer lead, so the omission is
 * deliberate rather than forgotten.
 *
 * EVERY REGISTER FACT BELOW IS FROM RM0455 Rev 13, which documents this part,
 * and NOT from RM0433, which documents its better-known sibling: I2C1 occupies
 * 0x40005400 to 0x400057FF (page 137), its clock is bit 21 I2C1EN of
 * RCC_APB1LENR (page 454), and the register map with every offset is Table 391
 * on page 1938.
 */
#ifndef BOARD_I2C_H
#define BOARD_I2C_H

#include <stdbool.h>
#include <stdint.h>

/* The refusals, each naming a thing that actually happens on this bus rather
 * than a generic failure. Negative, like board_status_t's, so a caller can test
 * for less than zero and still print which one. */
typedef enum {
    I2C_OK                  =  0,
    I2C_ERR_NOT_INITIALISED = -1,  /* i2c_init was not called, or it refused */
    I2C_ERR_TIMING          = -2,  /* no TIMINGR fields can reach the target rate
                                    * from this kernel clock */
    I2C_ERR_PERIPHERAL      = -3,  /* PE did not read back set: the peripheral
                                    * clock is not running */
    I2C_ERR_BUS_STUCK       = -4,  /* BUSY never cleared, which is a line held
                                    * low by a device mid-transfer or by wiring */
    I2C_ERR_NACK            = -5,  /* the address or the register byte was not
                                    * acknowledged; for a probe this is the
                                    * ordinary "nothing there" answer */
    I2C_ERR_ARBITRATION     = -6,  /* ARLO or BERR: another controller, or a
                                    * malformed condition on the wire */
    I2C_ERR_TIMEOUT         = -7,  /* a flag did not appear within the bounded
                                    * poll count, which is printed */
} i2c_status_t;

const char *i2c_status_name(i2c_status_t s);

/* What the bring-up computed and what the peripheral holds, so the console can
 * show both and a reader can check the arithmetic rather than trust it. Chapter
 * 6's rule, applied to a different peripheral: every step reports what it wrote
 * and what the register read back. */
typedef struct {
    uint32_t kernel_hz;      /* i2c_ker_ck as the caller stated it */
    uint32_t target_hz;      /* what was asked for */
    uint32_t achieved_hz;    /* what the chosen fields actually give */
    uint32_t timingr;        /* the word written */
    uint32_t timingr_read;   /* the word read back */
    uint8_t  presc;
    uint8_t  scll;
    uint8_t  sclh;
    uint8_t  sdadel;
    uint8_t  scldel;
} i2c_timing_t;

/* Bring I2C1 up as a controller on PB8 and PB9 at about target_hz.
 *
 * kernel_hz is i2c_ker_ck, which after reset is rcc_pclk1: the selection lives
 * in I2C123SEL and this function does not change it, so the caller states the
 * clock it believes is feeding the peripheral and the result is checked against
 * that belief rather than against an assumption made here.
 *
 * Returns I2C_OK and fills out, or a refusal with out left as far as it got. */
i2c_status_t i2c_init(uint32_t kernel_hz, uint32_t target_hz, i2c_timing_t *out);

/* Does anything answer at this 7-bit address? A write of zero bytes, which is
 * the cheapest legal way to ask: the address goes out, and the device either
 * acknowledges or does not.
 *
 * I2C_OK means something is there. I2C_ERR_NACK means nothing is, and that is
 * an ANSWER rather than a fault, which is why the roll call treats the two
 * differently from every other return. */
i2c_status_t i2c_probe(uint8_t addr7);

/* Read one register: write the register index, repeated start, read one byte.
 * The sensors on both shields use this form. */
i2c_status_t i2c_read_reg(uint8_t addr7, uint8_t reg, uint8_t *value);

#endif /* BOARD_I2C_H */
