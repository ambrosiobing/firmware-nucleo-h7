/* fifodecode.h: chapter 13's buffer decode, its drop counters and its sizing.
 *
 * WHAT THIS IS. The three pieces of chapter 13 that are arithmetic on a block of
 * bytes: the bounded decode of a drained buffer into per-source counts, the drop
 * count recovered from the tag counter's sequence, and the fill-to-drain ratio
 * that decides whether a watermark is safe. None of the three needs the shield,
 * the bus or the board, and all three are wrong in ways a ten-minute run would
 * not reveal, which is why they are here first.
 *
 * THE FIELD WIDTHS ARE PARAMETERS AND NOT CONSTANTS, deliberately. The chapter
 * writes TAG_SHIFT, CNT_SHIFT, CNT_MASK and WORD_BYTES as symbols and its Sources
 * section sends the tag format to each sensor's own datasheet, with the note that
 * the buffer depth "is not assumed from a sibling part". Only the word layout is
 * stated in the chapter, as a tag byte and six data bytes. So this file takes the
 * layout as a struct and the findings below hold for any counter field narrower
 * than a byte rather than for one guessed width.
 *
 * FINDING ONE: THE CHAPTER'S DROP COUNT IS COMPUTED IN THE WRONG MODULUS. Step 7
 * recovers drops from the tag counter:
 *
 *     if (cnt != expect_cnt) drops += (uint8_t)(cnt - expect_cnt);
 *
 * The subtraction wraps at 256 while the counter field wraps at its own width. A
 * two-bit counter that was expecting 3 and sees 0 has advanced by one, and that
 * line adds 253. The gap has to be taken in the field's modulus, which is what
 * fifo_cnt_gap() does, and the error is larger the narrower the field is.
 *
 * FINDING TWO: IT FABRICATES DROPS AT STARTUP. expect_cnt is a static beginning
 * at zero with no flag for the first word, so the first word of the first drain
 * is compared against zero. The buffer has been batching since it was configured
 * and the first counter value observed is whatever it had reached, so the logger
 * opens by reporting that many drops which never happened. A drop counter whose
 * first reading is fiction is worse than none, because it is the number the
 * ten-minute acceptance criterion is read from.
 *
 * FINDING THREE: A GAP OF EXACTLY THE COUNTER'S MODULUS IS INVISIBLE, and nothing
 * in the chapter says so. With an n-bit counter, 2^n consecutive lost slots read
 * as no loss at all. That is a bound on what the second drop mechanism can see,
 * it belongs beside the number, and fifo_budget() reports it.
 *
 * FINDING FOUR: THE FILL TIME DIVIDES BY ONE SOURCE'S RATE WHILE THE BUFFER
 * HOLDS SEVERAL. Step 4 batches both the accelerometer and the gyroscope at
 * 104 Hz, step 6 decodes both tags, and step 8 prints "rate 104 Hz watermark 128
 * words fill 1230 ms", which is 128 / 104 to the millisecond. Two sources fill it
 * at 208 entries a second, so the fill time is 615 ms and the safety ratio step 8
 * requires is optimistic by the number of batched sources. fifo_budget() sums the
 * rates and reports the chapter's form beside its own so the difference is a
 * number rather than an argument.
 *
 * WHAT IS NOT HERE. The bus shim, the identification registers, the watermark
 * route, the empty handler, the overrun flag, the ten-minute run and the
 * comparison against polling. Those need the IKS4A1 shield, which the inventory
 * does not hold. There is no measured number in this project.
 */
#ifndef FIFODECODE_H
#define FIFODECODE_H

#include <stdint.h>

/* A five-bit tag field is the widest this decode tabulates. The bound exists so
 * the counts can live on a caller's stack and so an unknown tag is a count
 * rather than an index into nothing. */
#define FIFO_TAG_VALUES 32u
#define FIFO_MAX_WORD_BYTES 16u
#define FIFO_MAX_WORDS 1024u
#define FIFO_MAX_SOURCES 8u

typedef struct {
    uint8_t word_bytes;  /* 7 in the chapter: one tag byte, six data bytes   */
    uint8_t tag_shift;   /* the source tag is w[0] >> tag_shift              */
    uint8_t cnt_shift;   /* the counter is (w[0] >> cnt_shift) & its mask    */
    uint8_t cnt_bits;    /* the counter's width in bits, 1 to 8              */
    uint32_t known_tags; /* one bit per tag value this logger expects        */
} fifo_fmt_t;

typedef struct {
    uint16_t words;                      /* words decoded across all calls  */
    uint16_t per_tag[FIFO_TAG_VALUES];   /* words seen, by source tag       */
    uint16_t unknown_tag;                /* words whose tag was not expected */
    uint32_t drops;                      /* summed gaps, in the right modulus */
    uint16_t gaps;                       /* how many gaps were non-zero     */
    uint8_t expect_cnt;                  /* carried between drains          */
    uint8_t started;                     /* 0 until the first word is seen  */
} fifo_state_t;

typedef struct {
    uint16_t watermark_words;
    uint8_t sources;                     /* how many are batched into it    */
    uint8_t word_bytes;
    uint8_t cnt_bits;
    double batch_hz[FIFO_MAX_SOURCES];   /* one rate per batched source     */
    double bus_hz;                       /* 400000 for fast mode            */
    double decode_us_per_word;           /* measured with the cycle counter */
} fifo_budget_t;

typedef struct {
    double entries_per_s;      /* the sum of the batching rates             */
    double fill_ms;            /* watermark / entries_per_s                 */
    double chapter_fill_ms;    /* watermark / batch_hz[0], the printed form */
    double drain_us;           /* the transaction plus the decode           */
    double ratio;              /* fill / drain, which step 8 wants above 10 */
    uint32_t undetectable_gap; /* 1 << cnt_bits, finding three              */
    int ratio_ok;              /* 1 when the order of magnitude is there    */
} fifo_budget_out_t;

#define FIFO_ERR_ARGUMENT (-1) /* a null pointer                            */
#define FIFO_ERR_FORMAT   (-2) /* a word, shift or counter width out of range */
#define FIFO_ERR_WORDS    (-3) /* more words than the landing buffer holds   */
#define FIFO_ERR_RATE     (-4) /* a batching rate or bus rate at or below zero */
#define FIFO_ERR_SOURCES  (-5) /* no sources, or more than the bound allows  */

/* Clears the carried counter state. Called before the first drain, which is the
 * step the chapter's static variable cannot express. */
void fifo_reset(fifo_state_t *state);

/* The gap between an expected and an observed counter value, in the counter's
 * own modulus. This is the corrected form of step 7's subtraction. */
uint8_t fifo_cnt_gap(uint8_t expect, uint8_t cnt, uint8_t cnt_bits);

/* Decode one drained block. words is the buffer level already clamped by the
 * caller; a value above FIFO_MAX_WORDS is refused rather than clamped here,
 * because silently shortening a read is how a level register drives a decode
 * past the end of its landing buffer. */
int fifo_decode(const fifo_fmt_t *fmt, const uint8_t *buf, uint16_t words,
                fifo_state_t *state);

/* The fill-to-drain ratio of step 8, with the chapter's own form reported
 * beside it. */
int fifo_budget(const fifo_budget_t *in, fifo_budget_out_t *out);

#endif /* FIFODECODE_H */
