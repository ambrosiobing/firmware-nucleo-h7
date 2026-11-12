/* projects/P13-iks4a1-fifo/c/rollcall.c: the identification roll call.
 *
 * WHAT THIS IMAGE IS AND WHAT IT IS NOT. Chapter 13's fifth acceptance
 * criterion is "the identification roll call passes for every device on the
 * chosen bus arrangement, and a deliberately wrong address in the table
 * produces a named error", and its sixth is that the arrangement is recorded
 * and "the firmware refuses to run if the devices it expects are not there".
 * This image is those two and nothing else. There is no FIFO here, no
 * watermark interrupt and no logging; those are the rest of the chapter and
 * they are worthless until this passes.
 *
 * IT IS ALSO THE FIRST THING IN THIS VOLUME TO DRIVE I2C AT ALL, so it is
 * written to make its own bring-up visible: the timing fields are printed with
 * the rate they produce, the pads are read back, and every refusal has a name.
 *
 * ONE SHIELD AT A TIME. The X-NUCLEO-IKS4A1, the X-NUCLEO-IKS5A1 and the
 * X-NUCLEO-53L8A1 fit the same header and collide on bus addresses and on the
 * 3.3 V budget, which chapter 13 states as a standing rule. The sibling lab set
 * records the symptom of ignoring it: an address clash at 0x6A and a scan that
 * reports the wrong part. This image expects the IKS4A1 and says so in its
 * banner, so a capture taken with the wrong shield fitted is identifiable from
 * the console text alone.
 *
 * THE EXPECTED TABLE IS A CLAIM AND THE SCAN IS THE EVIDENCE, which is why
 * both are printed. The scan runs first and reports everything that answers,
 * including addresses the table does not mention; the comparison happens
 * afterwards. A roll call that only looked for what it expected could not
 * report the one thing most worth knowing, which is a device nobody listed.
 */
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "i2c.h"

/* The bus. 100 kHz and not the chapter's 400 kHz, deliberately: standard mode
 * is what a bring-up should establish first, every device on this shield
 * supports it, and the fast-mode timing has a rise-time budget this image has
 * no way to measure. Chapter 13's logger will raise it once a capture exists. */
#define I2C_TARGET_HZ   100000u

/* The 7-bit addresses the X-NUCLEO-IKS4A1 is expected to answer at.
 *
 * THE TABLE STARTED AS THE SIBLING LAB SET'S AND IS NOW THIS BOARD'S. Chapter
 * 13 names that lab set as authoritative for this shield's addresses, and the
 * first version here used it: 0x6A, 0x1E, 0x44, 0x5C and 0x38. Two of those
 * five were wrong for this shield, which the first trustworthy capture showed
 * and which the next paragraph sets out. The lab set's figures came from a Pi
 * looking at a bus these parts sit behind, so it was a reasonable source and
 * not a careless one; it was simply not a measurement of this arrangement.
 * UM3239 IS HELD ON THE AUTHORING LAPTOP AND NOW CONFIRMS EVERY ROW. This
 * paragraph said it was not, which was wrong twice over: the manual was in the
 * same Downloads folder as UM2408 throughout, and the same mistake had already
 * been made with UM2408 six days earlier. Read on Thursday 12 November 2026.
 *
 * Table 1 on page 7 gives the solder bridges that set each address, with the
 * factory default in bold, and the addresses as 8 bit read values. Halved, all
 * seven match the measurement of Friday 6 November 2026 exactly:
 *
 *   LIS2DUXS12    SB20  33h -> 0x19      LIS2MDL   fixed  3Ch -> 0x1E
 *   STTS22H       fixed 71h -> 0x38      SHT40AD1B fixed  89h -> 0x44
 *   LPS22DF       SB29  BBh -> 0x5D
 *   LSM6DSO16IS   SB35  D5h -> 0x6A      LSM6DSV16X SB15 D7h -> 0x6B
 *
 * AND PAGE 6 GIVES THE REASON THE TWO INERTIAL UNITS DIFFER, which this project
 * had worked out from the bus and the manual states outright: both parts answer
 * at D5h and D7h, so the board carries bridges "to avoid an address conflict on
 * I2C bus with the LSM6DSO16IS and the LSM6DSV16X". The addresses are not
 * properties of the parts; they are what this board's bridges select.
 *
 * THE IDENTIFICATION REGISTER IS NOT ALWAYS 0x0F, which is the detail that
 * makes a roll call report a present part as missing. The bench record of
 * Tuesday 27 October 2026 found the LIS2MDL answering its WHO_AM_I at 0x4F.
 * Both registers are therefore read from every responder and both are printed.
 *
 * MEASURED ON THIS BOARD ON FRIDAY 6 NOVEMBER 2026, and the table below is now
 * that measurement rather than the lab set's. The first trustworthy capture,
 * image B55D8693, found seven devices and settled two disagreements:
 *
 *   TWO INERTIAL UNITS, AT 0x6A AND 0x6B, answering 0x22 and 0x70. Chapter 13
 *   says the shield carries two and the lab set's table lists one row at 0x6A,
 *   and two parts cannot share an address. The chapter was right. 0x22 is the
 *   LSM6DSO16IS BY UM3239, SB35 at D5h, which is 0x6A. This paragraph used to
 *   name it from the bench record of Tuesday 27 October 2026, which found 0x22
 *   on the IKS5A1, and that reasoning was wrong even though the answer is
 *   right: chapter 14 holds four sources saying the IKS5A1 does not carry this
 *   part, so a value carried across from the other card could not have named
 *   it. 0x70 WAS AN INFERENCE AND IS NOW SOURCED: this
 *   paragraph said the LSM6DSV16X was identified by elimination because the
 *   shield carries exactly those two, and UM3239's Table 1 settles it, putting
 *   that part on SB15 at D7h, which is 0x6B. The inference happened to be
 *   right and was still an inference until Thursday 12 November 2026.
 *
 *   THE PRESSURE PART IS AT 0x5D AND NOT 0x5C. The same bench record says the
 *   difference between these two shields includes "pressure at 0x5c not 0x5d",
 *   and this measurement puts the 0xB4 at 0x5D, which is the IKS4A1 side of
 *   that sentence.
 *
 *   0x44 ANSWERS AND REFUSES A REGISTER READ, which is correct rather than
 *   broken: the SHT40AD1B takes commands, not register indices, so there is no
 *   index to send it. Its row asks for no register at all.
 *
 *   0x38's IDENTIFICATION REGISTER IS NOT 0x0F. It answered the address and
 *   returned zero from 0x0F, which is a register that happens to read zero
 *   rather than an identity. The STTS22H keeps its WHO_AM_I at 0x01, and
 *   asking there returned 0xA0 in image 3AF8847E, so the row carries that
 *   value measured on this board rather than quoted from a datasheet.
 *
 * EVERY VALUE IN THE TABLE IS NOW MEASURED ON THIS BOARD except 0x44's, which
 * has none to measure: six of the seven rows carry a value this bench read,
 * and the SHT40AD1B has no register interface to read one from. The rule that
 * produced that state is kept because the next shield will need it: a zero
 * means "print it, do not judge it", and a WHO_AM_I quoted from a datasheet
 * this laptop cannot open would be the kind of unchecked figure this volume
 * keeps having to withdraw. */
typedef struct {
    uint8_t     addr7;
    uint8_t     who_reg;
    uint8_t     who_expect;   /* 0 means unmeasured here: report, do not judge */
    const char *name;
} expected_t;

static const expected_t EXPECTED[] = {
    { 0x19u, 0x0Fu, 0x47u, "LIS2DUXS12 accelerometer" },
    { 0x1Eu, 0x4Fu, 0x40u, "LIS2MDL magnetometer" },
    { 0x38u, 0x01u, 0xA0u, "STTS22H temperature" },
    { 0x44u, 0x00u, 0x00u, "SHT40AD1B humidity, no register interface" },
    { 0x5Du, 0x0Fu, 0xB4u, "LPS22DF barometer" },
    { 0x6Au, 0x0Fu, 0x22u, "LSM6DSO16IS inertial" },
    { 0x6Bu, 0x0Fu, 0x70u, "LSM6DSV16X inertial" },
};
#define EXPECTED_COUNT  (sizeof EXPECTED / sizeof EXPECTED[0])

/* The scan range. 0x00 to 0x07 and 0x78 to 0x7F are reserved by the I2C
 * specification and are not probed, which is what i2cdetect does and for the
 * same reason: a general call to 0x00 is not a question about a device. */
#define SCAN_FIRST  0x08u
#define SCAN_LAST   0x77u

static bool answered[SCAN_LAST + 1u];

static void banner(const i2c_timing_t *t)
{
    printf("\r\n");
    printf("P13 roll call, X-NUCLEO-IKS4A1 expected, one shield only\r\n");
    printf("  bus      I2C1 on PB8 SCL and PB9 SDA, Arduino D15 and D14,\r\n");
    printf("           CN7 pins 2 and 4, alternate function 4, open drain\r\n");
    /* THE BUILD STAMP, and the reason it is here rather than in a later
     * tidy-up. On Friday 6 November 2026 a flash step was skipped twice and
     * the board answered with the previous image, printing a banner identical
     * to the new one down to the last character; only a file hash taken
     * outside the board could tell the two runs apart. __DATE__ and __TIME__
     * are this FILE's compile time and not the link time, which P06's banner
     * explains is the weaker of the two stamps, and they move whenever this
     * file is recompiled, which is every time the expected table changes.
     * P06's stronger stamp reads the linker symbols, and that needs the
     * pointer cast which keeps its main.c out of the host warning sweep; that
     * trade is not worth making for this image. */
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
    /* An upper bound and labelled as one: the wire's rise time is not in the
     * arithmetic, so a scope would read this or slower, never faster. */
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
            /* Anything that is not a plain refusal to answer is a fault on the
             * wire and is worth a line of its own: a scan that quietly treated
             * a stuck bus as 112 absent devices would report an empty shield. */
            printf("  0x%02X  %s\r\n", a, i2c_status_name(s));
        }
    }
    printf("  %u device(s) answered\r\n\r\n", found);
    return found;
}

/* The comparison, and the refusals criterion 6 asks for. */
static bool roll_call(void)
{
    bool ok = true;
    printf("roll call against the expected X-NUCLEO-IKS4A1:\r\n");
    for (unsigned i = 0u; i < EXPECTED_COUNT; i++) {
        const expected_t *e = &EXPECTED[i];
        if (!answered[e->addr7]) {
            printf("  0x%02X  MISSING   %s\r\n", e->addr7, e->name);
            ok = false;
            continue;
        }
        if (e->who_reg == 0x00u) {
            printf("  0x%02X  present   %s\r\n", e->addr7, e->name);
            continue;
        }
        uint8_t v = 0u;
        const i2c_status_t r = i2c_read_reg(e->addr7, e->who_reg, &v);
        if (r != I2C_OK) {
            printf("  0x%02X  UNREAD    reg 0x%02X %s, %s\r\n",
                   e->addr7, e->who_reg, i2c_status_name(r), e->name);
            ok = false;
        } else if (e->who_expect == 0u) {
            printf("  0x%02X  present   reg 0x%02X = 0x%02X, unmeasured here, %s\r\n",
                   e->addr7, e->who_reg, v, e->name);
        } else if (v != e->who_expect) {
            printf("  0x%02X  WRONG     reg 0x%02X = 0x%02X, expected 0x%02X, %s\r\n",
                   e->addr7, e->who_reg, v, e->who_expect, e->name);
            ok = false;
        } else {
            printf("  0x%02X  confirmed reg 0x%02X = 0x%02X, %s\r\n",
                   e->addr7, e->who_reg, v, e->name);
        }
    }

    /* And the other direction, which is the half a table-driven check usually
     * leaves out: something answered that nothing expects. On this bus that is
     * either the wrong shield fitted or a part the written record has wrong,
     * and chapter 13 already suspects the second, because it says the shield
     * carries two inertial units while the lab set lists one address for them. */
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
        printf("\r\nP13 roll call REFUSED at bring-up: %s\r\n",
               i2c_status_name(init));
        printf("  nothing was scanned, which is not the same as an empty bus\r\n");
        for (;;) { }
    }

    banner(&t);
    const unsigned found = scan();

    if (found == 0u) {
        /* The case that makes one source for the alternate function tolerable.
         * A wrong function number leaves both pads driving nothing, and this is
         * where that shows: an empty bus is a refusal, never a pass. */
        printf("REFUSED: nothing answered anywhere in the scan range.\r\n");
        printf("  Either no shield is fitted, or the pads are not on the bus.\r\n");
        printf("  The alternate function above rests on one source; an empty\r\n");
        printf("  scan is the symptom a wrong one would produce.\r\n");
        for (;;) { }
    }

    if (!roll_call()) {
        printf("\r\nREFUSED: the bus is not the expected X-NUCLEO-IKS4A1.\r\n");
        for (;;) { }
    }

    printf("\r\nPASS: every expected device answered and identified.\r\n");
    for (;;) { }
}
