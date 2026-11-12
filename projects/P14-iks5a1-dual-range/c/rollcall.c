/* projects/P14-iks5a1-dual-range/c/rollcall.c: the identification roll call.
 *
 * THE SAME IMAGE AS P13'S, WITH A DIFFERENT TABLE, and that is the point rather
 * than a shortcut. `c/board/i2c.c` brings up I2C1, scans and reads registers;
 * nothing in it is specific to a shield. What a project contributes is the list
 * of devices it expects and the refusal when the bus is not that list. If this
 * file needed anything else from the bus layer, the bus layer would be wrong.
 *
 * WHAT IT PROVES THAT P13'S COULD NOT. P13's roll call could fail only by
 * finding nothing or finding the unexpected. This one can fail by finding the
 * OTHER SHIELD, because both cards answer at 0x19, 0x1E, 0x6A and 0x6B, so an
 * address scan alone cannot tell them apart. The values separate them, and this
 * image refuses on the values. Chapter 13 states the one-shield-at-a-time rule
 * and the sibling lab set records the symptom of breaking it; here the firmware
 * enforces it instead of a sentence asking a reader to.
 *
 * THE DISCRIMINATORS, from the two measurements and UM3239. There are five
 * rows and the heading used to say three, which was the count of the ones
 * written into IKS4A1_SIGNS below rather than the count of the table:
 *
 *             IKS5A1 (this)        IKS4A1 (P13's)
 *   0x38      absent               present, 0x01 = 0xA0
 *   0x44      absent               present, no register interface
 *   pressure  0x5C                 0x5D
 *   0x6A      0x73                 0x22, the LSM6DSO16IS by UM3239
 *   0x6B      0x22, unnamed here   0x70, the LSM6DSV16X by UM3239
 *
 * Both named parts in that right-hand column are the other card's and neither
 * is on this shield, which is the whole reason the left column names nothing.
 *
 * So fitting the IKS4A1 by mistake is noticed six independent ways, which is
 * the number the capture of Monday 9 November 2026 actually printed: 0x5C
 * missing, 0x5D unlisted, 0x38 unlisted, 0x44 unlisted, the wrong value at
 * 0x6A and the wrong value at 0x6B. This sentence said four and listed five,
 * having dropped the second inertial row twice over; the count is written out
 * item by item now so the number cannot drift from the list again.
 */
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "i2c.h"

/* 100 kHz, for P13's reason: standard mode is what a bring-up establishes
 * first, and the fast-mode rise-time budget is not something this image can
 * measure. Chapter 14's own table asks for 400 kHz and that waits for a
 * capture. */
#define I2C_TARGET_HZ   100000u

/* The X-NUCLEO-IKS5A1, measured on the bench on Tuesday 27 October 2026 with
 * the shield on flying leads to a Pi 3B+, parts identified by command set
 * rather than by address. That measurement is the source for every row here,
 * and it is a different bus from this one: the Pi reached the shield directly
 * on its own header pins, while this board reaches it through the Arduino
 * connector. The addresses are the devices' own and do not depend on the
 * master, so they carry over; anything that does not is what this image is for.
 *
 * NO VENDOR MANUAL BACKS THIS TABLE, and that is now a deliberate difference
 * rather than an oversight. P13's equivalent is confirmed row by row by
 * UM3239, read on Thursday 12 November 2026, which gives the solder bridge
 * setting each address on the IKS4A1. The corresponding manual for this shield
 * is UM3550, named in AUTHORING.md, and it is NOT on the authoring laptop:
 * Downloads holds UM2408, UM3239 and the IKS4A1 data brief and nothing for the
 * IKS5A1. So this table rests on one measurement from one bus, and the capture
 * that would confirm it from a second master does not exist yet. Say which
 * document is missing rather than that none exists; the first can be fetched.
 *
 * NEITHER INERTIAL ROW CARRIES A PART NAME, AND ONE OF THEM USED TO.
 *
 *   0x6A answered 0x73, which is in no driver table the Pi held, so that row
 *   never had a name. Printing an unnamed value is honest; guessing a part
 *   number from a datasheet this laptop cannot open is how a wrong figure
 *   becomes a cited one.
 *
 *   0x6B answered 0x22 and this table called it the LSM6DSO16IS until
 *   Thursday 12 November 2026, because the Pi's driver table mapped 0x22 to
 *   that part. CHAPTER 14 RULES THAT PART OFF THIS SHIELD: four sources,
 *   the vendor's product page among them, give the complement as ISM6HG256X,
 *   ISM330IS, IIS2DULPX, IIS2MDC and ILPS22QS, and the chapter's step 1 says
 *   in as many words that finding an LSM6DSO16IS means holding a different
 *   board. The ISM330IS is that part's industrial sibling with the same core,
 *   which is the likeliest reason one identification value matched two
 *   catalogue numbers. So 0x22 here is NOT evidence of the LSM6DSO16IS; it is
 *   evidence that an identification value does not name a part across a
 *   product family, and until UM3550 is on this laptop the row stays unnamed.
 *
 * THE WIDER CORRECTION. P14's own README and the journal of Monday 9 November
 * 2026 presented the two cards as strapping ONE SHARED PART to swapped
 * addresses, 0x6A on the IKS4A1 and 0x6B here. That reading is withdrawn. The
 * cards carry DIFFERENT PARTS that answer the same value, which is a sharper
 * version of the same lesson and a worse trap: a swapped address can be found
 * by a scan, while two siblings sharing an identification value cannot be
 * separated by any register this image reads.
 *
 * THE 0x4F ROW IS THE ONE THAT CATCHES PEOPLE. The LIS2MDL keeps its WHO_AM_I
 * at 0x4F and not at 0x0F, so a roll call reading only 0x0F reports a present
 * part as missing. Both registers are read from every responder. */
typedef struct {
    uint8_t     addr7;
    uint8_t     who_reg;
    uint8_t     who_expect;   /* 0 means unmeasured here: report, do not judge */
    const char *name;
} expected_t;

static const expected_t EXPECTED[] = {
    { 0x19u, 0x0Fu, 0x47u, "LIS2DUXS12 accelerometer" },
    { 0x1Eu, 0x4Fu, 0x40u, "LIS2MDL magnetometer" },
    { 0x5Cu, 0x0Fu, 0xB4u, "LPS22DF or LPS28DFW pressure" },
    { 0x6Au, 0x0Fu, 0x73u, "inertial, unnamed: 0x73 is in no driver table here" },
    { 0x6Bu, 0x0Fu, 0x22u, "inertial, unnamed: 0x22 named a part not on this card" },
};
#define EXPECTED_COUNT  (sizeof EXPECTED / sizeof EXPECTED[0])

/* The addresses that would mean the IKS4A1 is fitted instead. Reported by name
 * rather than as a bare surprise, because "0x38 answered and nothing expects
 * it" and "you have fitted the other shield" are the same fact and only one of
 * them tells you what to do. */
typedef struct {
    uint8_t     addr7;
    const char *why;
} wrong_shield_t;

static const wrong_shield_t IKS4A1_SIGNS[] = {
    { 0x38u, "STTS22H, which the IKS5A1 does not carry" },
    { 0x44u, "SHT40AD1B, which the IKS5A1 does not carry" },
    { 0x5Du, "pressure at 0x5D, where this shield puts it at 0x5C" },
};
#define IKS4A1_SIGN_COUNT (sizeof IKS4A1_SIGNS / sizeof IKS4A1_SIGNS[0])

#define SCAN_FIRST  0x08u
#define SCAN_LAST   0x77u

static bool answered[SCAN_LAST + 1u];

static void banner(const i2c_timing_t *t)
{
    printf("\r\n");
    printf("P14 roll call, X-NUCLEO-IKS5A1 expected, one shield only\r\n");
    printf("  bus      I2C1 on PB8 SCL and PB9 SDA, Arduino D15 and D14,\r\n");
    printf("           CN7 pins 2 and 4, alternate function 4, open drain\r\n");
    printf("  built    %s %s, this file's compile time\r\n", __DATE__, __TIME__);
    printf("  core     %lu Hz\r\n", (unsigned long) board_core_hz());
    printf("  i2c_ker  %lu Hz, taken as rcc_pclk1\r\n",
           (unsigned long) t->kernel_hz);
    printf("  asked    %lu Hz\r\n", (unsigned long) t->target_hz);
    printf("  TIMINGR  wrote %08lX read back %08lX\r\n",
           (unsigned long) t->timingr, (unsigned long) t->timingr_read);
    printf("           PRESC %u SCLL %u SCLH %u SDADEL %u SCLDEL %u\r\n",
           (unsigned) t->presc, (unsigned) t->scll, (unsigned) t->sclh,
           (unsigned) t->sdadel, (unsigned) t->scldel);
    printf("  computed %lu Hz, an upper bound: no rise time in the model\r\n",
           (unsigned long) t->achieved_hz);
    printf("\r\n");
}

static unsigned scan(void)
{
    unsigned found = 0u;
    printf("scan 0x%02X to 0x%02X:\r\n", SCAN_FIRST, SCAN_LAST);
    for (unsigned a = SCAN_FIRST; a <= SCAN_LAST; a++) {
        const i2c_status_t s = i2c_probe((uint8_t) a);
        if (s == I2C_OK) {
            answered[a] = true;
            found++;
            uint8_t v0 = 0u, v4 = 0u;
            const i2c_status_t r0 = i2c_read_reg((uint8_t) a, 0x0Fu, &v0);
            const i2c_status_t r4 = i2c_read_reg((uint8_t) a, 0x4Fu, &v4);
            printf("  0x%02X  answers", a);
            if (r0 == I2C_OK) { printf("   reg 0x0F = 0x%02X", v0); }
            else              { printf("   reg 0x0F %s", i2c_status_name(r0)); }
            if (r4 == I2C_OK) { printf("   reg 0x4F = 0x%02X", v4); }
            else              { printf("   reg 0x4F %s", i2c_status_name(r4)); }
            printf("\r\n");
        } else if (s != I2C_ERR_NACK) {
            printf("  0x%02X  %s\r\n", a, i2c_status_name(s));
        }
    }
    printf("  %u device(s) answered\r\n\r\n", found);
    return found;
}

/* Is this the other card? Checked before the roll call, so the report leads
 * with the useful sentence instead of five confusing ones. */
static unsigned wrong_shield(void)
{
    unsigned signs = 0u;
    for (unsigned i = 0u; i < IKS4A1_SIGN_COUNT; i++) {
        if (answered[IKS4A1_SIGNS[i].addr7]) {
            if (signs == 0u) {
                printf("THIS LOOKS LIKE THE X-NUCLEO-IKS4A1, not the IKS5A1:\r\n");
            }
            printf("  0x%02X  %s\r\n", IKS4A1_SIGNS[i].addr7, IKS4A1_SIGNS[i].why);
            signs++;
        }
    }
    if (signs != 0u) {
        printf("  Both shields answer at 0x19, 0x1E, 0x6A and 0x6B, so the\r\n");
        printf("  addresses alone cannot separate them; the values above can.\r\n");
        printf("  Fit one shield at a time, and reflash before every capture.\r\n\r\n");
    }
    return signs;
}

static bool roll_call(void)
{
    bool ok = true;
    printf("roll call against the expected X-NUCLEO-IKS5A1:\r\n");
    for (unsigned i = 0u; i < EXPECTED_COUNT; i++) {
        const expected_t *e = &EXPECTED[i];
        if (!answered[e->addr7]) {
            printf("  0x%02X  MISSING   %s\r\n", e->addr7, e->name);
            ok = false;
            continue;
        }
        uint8_t v = 0u;
        const i2c_status_t r = i2c_read_reg(e->addr7, e->who_reg, &v);
        if (r != I2C_OK) {
            printf("  0x%02X  UNREAD    reg 0x%02X %s, %s\r\n",
                   e->addr7, e->who_reg, i2c_status_name(r), e->name);
            ok = false;
        } else if (v != e->who_expect) {
            printf("  0x%02X  WRONG     reg 0x%02X = 0x%02X, expected 0x%02X, %s\r\n",
                   e->addr7, e->who_reg, v, e->who_expect, e->name);
            ok = false;
        } else {
            printf("  0x%02X  confirmed reg 0x%02X = 0x%02X, %s\r\n",
                   e->addr7, e->who_reg, v, e->name);
        }
    }

    for (unsigned a = SCAN_FIRST; a <= SCAN_LAST; a++) {
        if (!answered[a]) {
            continue;
        }
        bool listed = false;
        for (unsigned i = 0u; i < EXPECTED_COUNT; i++) {
            if (EXPECTED[i].addr7 == a) { listed = true; break; }
        }
        if (!listed) {
            printf("  0x%02X  UNLISTED  answered and no entry expects it\r\n", a);
            ok = false;
        }
    }
    return ok;
}

int main(void)
{
    board_init();

    i2c_timing_t t = { 0 };
    const i2c_status_t init = i2c_init(board_pclk1_hz(), I2C_TARGET_HZ, &t);
    if (init != I2C_OK) {
        printf("\r\nP14 roll call REFUSED at bring-up: %s\r\n",
               i2c_status_name(init));
        printf("  nothing was scanned, which is not the same as an empty bus\r\n");
        for (;;) { }
    }

    banner(&t);
    const unsigned found = scan();

    if (found == 0u) {
        printf("REFUSED: nothing answered anywhere in the scan range.\r\n");
        printf("  Either no shield is fitted, or the pads are not on the bus.\r\n");
        for (;;) { }
    }

    const unsigned signs = wrong_shield();
    const bool listed_ok = roll_call();

    if (signs != 0u) {
        printf("\r\nREFUSED: the wrong shield appears to be fitted.\r\n");
        for (;;) { }
    }
    if (!listed_ok) {
        printf("\r\nREFUSED: the bus is not the expected X-NUCLEO-IKS5A1.\r\n");
        for (;;) { }
    }

    printf("\r\nPASS: every expected device answered and identified.\r\n");
    for (;;) { }
}
