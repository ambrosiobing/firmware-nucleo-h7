/* c/clock/clocktree.h: the clock tree decoded from register VALUES, on any host.
 *
 * WHY THIS FILE EXISTS, AND IT IS NOT TO HAVE A SECOND COPY OF ANYTHING.
 *
 * Until Sunday 4 October 2026 the decode lived inside c/board/system.c as four
 * static functions that read RCC directly. That made it correct and untestable
 * in the same stroke: every one of them dereferences a memory mapped address, so
 * nothing on a host could call them, and the only way to find out whether the
 * decode was right was to flash the board and believe the banner.
 *
 * It was right. The measurement on Sunday 4 October 2026 proved it, by agreeing
 * with the decoded 280 MHz to 1168 parts per million and tracing that remainder
 * to the input frequency rather than to any field. But "it was right" is a fact
 * about one configuration out of the many these registers can hold, and the
 * configurations that matter most are the refusals, which the board has no
 * convenient way to produce on demand.
 *
 * So the decode moved here, unchanged in behaviour, taking the register values
 * as an argument instead of reading them. system.c now reads the seven words and
 * calls this. There is still exactly one copy of the field rules, and now a host
 * can drive it through every refusal it has.
 *
 * WHY IT INCLUDES THE REGISTER HEADER AND MUST NEVER DEREFERENCE IT.
 * stm32h7a3_regs.h is where the masks and positions are sourced, each with the
 * authority that settled it, so this file includes it rather than restating a
 * single shift. Those register names are REG32 expression macros, which are
 * harmless to declare and fatal to evaluate off the part. Nothing in
 * clocktree.c may name one; python/tests/test_clocktree.py checks that it does
 * not, because a stray RCC_CR here would compile cleanly and then read address
 * 0x58024400 on the host.
 *
 * WHAT IT ADDS BEYOND THE MOVE. system.c could only ever say "unconfirmed". It
 * had eight distinct reasons to refuse and reported one value for all of them.
 * This returns which, so a console can print the reason and a test can assert
 * that the right guard fired rather than that some guard did.
 */
#ifndef CLOCKTREE_H
#define CLOCKTREE_H

#include <stdint.h>

/* The seven words the decode reads, and nothing else. Named for the registers
 * they come from so a dump pasted off the console maps to them by eye. */
typedef struct {
    uint32_t cr;          /* RCC_CR:        HSIDIV, HSEBYP, HSERDY */
    uint32_t cfgr;        /* RCC_CFGR:      SWS, which source actually drives sys_ck */
    uint32_t pllckselr;   /* RCC_PLLCKSELR: PLLSRC and DIVM1 */
    uint32_t pllcfgr;     /* RCC_PLLCFGR:   PLL1FRACEN and DIVP1EN */
    uint32_t pll1divr;    /* RCC_PLL1DIVR:  N1 and P1 */
    uint32_t cdcfgr1;     /* RCC_CDCFGR1:   CDCPRE and HPRE */
    uint32_t cdcfgr2;     /* RCC_CDCFGR2:   CDPPRE1 */
} clocktree_regs_t;

/* Why the decode gave up. These are the eight paths system.c collapsed into one
 * status, and the order is the order they are tested in. */
typedef enum {
    CLOCKTREE_OK                  = 0,
    CLOCKTREE_SWS_UNKNOWN         = 1,  /* CSI drives sys_ck, and its nominal is
                                         * not sourced anywhere in this volume */
    CLOCKTREE_HSE_NOT_BYPASS      = 2,  /* HSEBYP or HSERDY clear, so 8 MHz would
                                         * be asserting a board fact that does
                                         * not apply to whatever is out there */
    CLOCKTREE_PLL_FRACTIONAL      = 3,  /* FRACEN set and FRACN is not decoded */
    CLOCKTREE_PLL_P_DISABLED      = 4,  /* the P output is off, so it is not
                                         * driving anything, whatever SWS says */
    CLOCKTREE_PLL_SOURCE_UNKNOWN  = 5,  /* PLLSRC is CSI, or the source refused */
    CLOCKTREE_PLL_ZERO_DIVIDER    = 6,  /* DIVM1 is 0, which hardware permits */
    CLOCKTREE_PLL_WOULD_OVERFLOW  = 7,  /* ref/M times N exceeds 32 bits */
    CLOCKTREE_PRESCALER_UNDECODED = 8   /* a CDCPRE, HPRE or CDPPRE1 ratio outside
                                         * the three this volume has sourced */
} clocktree_refusal_t;

typedef struct {
    uint32_t            sys_hz;    /* 0 unless refusal is CLOCKTREE_OK */
    uint32_t            core_hz;
    uint32_t            ahb_hz;
    uint32_t            pclk1_hz;
    clocktree_refusal_t refusal;
} clocktree_t;

/* Decode, or say why not. hsi_nominal and hse_bypass are passed in rather than
 * taken from the header for one reason: a test that wants to ask what the tree
 * would be at a different input frequency can, and after Sunday 4 October 2026
 * that is not a hypothetical question. The board's HSE is 7990652 Hz and its
 * nominal is 8000000, and both belong in the vector file. */
void clocktree_decode(const clocktree_regs_t *r,
                      uint32_t hsi_nominal,
                      uint32_t hse_bypass,
                      clocktree_t *out);

/* THE SIGNED BIAS, which exists because a sentence got it wrong three times.
 *
 * "About 0.117 per cent high" was written into three files on Sunday 4 October
 * 2026 and it is not a statement about anything in particular. The two clocks
 * this board runs at err in OPPOSITE directions, and at either clock the answer
 * depends on which way the arithmetic goes. Three quantities, and they are not
 * one quantity:
 *
 *   frequency_ppm  what board_core_hz() reports, against the truth
 *   duration_ppm   an interval measured by counting cycles and dividing by the
 *                  reported frequency
 *   delay_ppm      an interval requested by computing cycles from the reported
 *                  frequency and spinning
 *
 * delay_ppm EQUALS frequency_ppm, by the same expression, and that is a result
 * rather than a shortcut: both divide by the true frequency. duration_ppm has
 * the reported frequency underneath instead, so it differs in the last two or
 * nine parts per million. Knowing they are two numbers and not three is most of
 * the value here.
 *
 * ROUNDING IS SPECIFIED because four languages must agree on it: integer
 * arithmetic throughout, no floating point, and halves go away from zero. The
 * intermediate is 64 bit, which it has to be: 327178 times a million is 3.3e14.
 *
 * Both frequencies must be non zero. Returns false and zeroes the output
 * otherwise, because a bias against an unknown truth is not a small bias. */
typedef struct {
    int32_t frequency_ppm;
    int32_t duration_ppm;
    int32_t delay_ppm;
} clocktree_bias_t;

int clocktree_bias(uint32_t reported_hz, uint32_t true_hz, clocktree_bias_t *out);

/* The refusal as a short stable token, never a sentence.
 *
 * WHY A TOKEN AND NOT A MESSAGE. These strings go three places: a console line
 * on the board, a line protocol that the C++ and Rust filters answer on, and the
 * parity test that compares all four languages. A sentence would make the first
 * nicer and the other two fragile, and the first is the one with a README beside
 * it. So they are lower case, hyphenated, and the same nine in every language,
 * which means the parity test compares reasons rather than only numbers.
 *
 * Returns "unknown" for a value outside the enum, which is reachable only by
 * casting an integer in and is therefore worth having rather than asserting. */
const char *clocktree_refusal_text(clocktree_refusal_t why);

#endif  /* CLOCKTREE_H */
