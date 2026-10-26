/* fifodecode.c: the decode, the gap in the right modulus, and the ratio.
 *
 * The header says at length why the gap is taken in the counter's own modulus
 * and why the first word of the first drain cannot be compared against zero.
 */
#include "fifodecode.h"

#include <stddef.h>

static int format_ok(const fifo_fmt_t *fmt)
{
    if (fmt->word_bytes == 0u || fmt->word_bytes > FIFO_MAX_WORD_BYTES) {
        return 0;
    }
    if (fmt->cnt_bits == 0u || fmt->cnt_bits > 8u) {
        return 0;
    }
    if (fmt->tag_shift > 7u || fmt->cnt_shift > 7u) {
        return 0;
    }
    /* The tag field is whatever remains above tag_shift, so a shift that leaves
     * room for more values than the table has is a format this cannot tabulate
     * rather than one it should silently truncate. */
    if ((0xFFu >> fmt->tag_shift) >= FIFO_TAG_VALUES) {
        return 0;
    }
    return 1;
}

void fifo_reset(fifo_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->words = 0u;
    for (uint32_t t = 0u; t < FIFO_TAG_VALUES; t++) {
        state->per_tag[t] = 0u;
    }
    state->unknown_tag = 0u;
    state->drops = 0u;
    state->gaps = 0u;
    state->expect_cnt = 0u;
    state->started = 0u;
}

uint8_t fifo_cnt_gap(uint8_t expect, uint8_t cnt, uint8_t cnt_bits)
{
    if (cnt_bits == 0u || cnt_bits > 8u) {
        return 0u;
    }
    /* The modulus is the field's, not the byte's. With cnt_bits of 8 the mask is
     * 0xFF and this is the chapter's subtraction; below 8 it is not, and the
     * narrower the field the larger the chapter's error. */
    const uint32_t mask = (cnt_bits >= 8u) ? 0xFFu : ((1u << cnt_bits) - 1u);
    const uint32_t gap = ((uint32_t) cnt - (uint32_t) expect) & mask;
    return (uint8_t) gap;
}

int fifo_decode(const fifo_fmt_t *fmt, const uint8_t *buf, uint16_t words,
                fifo_state_t *state)
{
    if (fmt == NULL || buf == NULL || state == NULL) {
        return FIFO_ERR_ARGUMENT;
    }
    if (format_ok(fmt) == 0) {
        return FIFO_ERR_FORMAT;
    }
    if (words > FIFO_MAX_WORDS) {
        return FIFO_ERR_WORDS;
    }

    const uint32_t cnt_mask = (fmt->cnt_bits >= 8u)
                            ? 0xFFu : ((1u << fmt->cnt_bits) - 1u);

    for (uint16_t i = 0u; i < words; i++) {
        const uint8_t *w = &buf[(uint32_t) i * (uint32_t) fmt->word_bytes];
        const uint8_t tag = (uint8_t) (w[0] >> fmt->tag_shift);
        const uint8_t cnt = (uint8_t) (((uint32_t) w[0] >> fmt->cnt_shift)
                                       & cnt_mask);

        if (((fmt->known_tags >> tag) & 1u) != 0u) {
            state->per_tag[tag]++;
        } else {
            state->unknown_tag++;
        }

        /* The first word of the first drain establishes the sequence instead of
         * being measured against a zero nobody wrote. This is the whole of
         * finding two, and it is two lines. */
        if (state->started == 0u) {
            state->started = 1u;
        } else {
            const uint8_t gap = fifo_cnt_gap(state->expect_cnt, cnt,
                                             fmt->cnt_bits);
            if (gap != 0u) {
                state->drops += (uint32_t) gap;
                state->gaps++;
            }
        }
        state->expect_cnt = (uint8_t) ((cnt + 1u) & cnt_mask);
        state->words++;
    }
    return 0;
}

int fifo_budget(const fifo_budget_t *in, fifo_budget_out_t *out)
{
    if (in == NULL || out == NULL) {
        return FIFO_ERR_ARGUMENT;
    }
    if (in->sources == 0u || in->sources > FIFO_MAX_SOURCES) {
        return FIFO_ERR_SOURCES;
    }
    if (in->word_bytes == 0u || in->word_bytes > FIFO_MAX_WORD_BYTES) {
        return FIFO_ERR_FORMAT;
    }
    if (in->cnt_bits == 0u || in->cnt_bits > 8u) {
        return FIFO_ERR_FORMAT;
    }
    if (in->watermark_words == 0u || in->watermark_words > FIFO_MAX_WORDS) {
        return FIFO_ERR_WORDS;
    }
    if (!(in->bus_hz > 0.0) || !(in->decode_us_per_word >= 0.0)) {
        return FIFO_ERR_RATE;
    }
    for (uint8_t s = 0u; s < in->sources; s++) {
        if (!(in->batch_hz[s] > 0.0)) {
            return FIFO_ERR_RATE;
        }
    }

    /* Every batched source writes its own entries into one buffer, so the buffer
     * fills at the sum of the rates. The chapter's printed form divides by one
     * of them, and both are reported. */
    double entries = 0.0;
    for (uint8_t s = 0u; s < in->sources; s++) {
        entries += in->batch_hz[s];
    }
    out->entries_per_s = entries;
    out->fill_ms = 1000.0 * (double) in->watermark_words / entries;
    out->chapter_fill_ms = 1000.0 * (double) in->watermark_words
                         / in->batch_hz[0];

    /* The transaction, as a stated model rather than a measurement: every byte
     * on this bus costs nine bits with its acknowledgement, the register address
     * and the device address are two more bytes, and a start and a stop bracket
     * the whole thing. The decode cost is measured on the board with the cycle
     * counter and arrives here as a parameter. */
    const double bytes = (double) in->watermark_words * (double) in->word_bytes;
    const double bits = (bytes + 2.0) * 9.0 + 2.0;
    out->drain_us = 1.0e6 * bits / in->bus_hz
                  + (double) in->watermark_words * in->decode_us_per_word;

    out->ratio = (out->fill_ms * 1000.0) / out->drain_us;
    out->ratio_ok = (out->ratio >= 10.0) ? 1 : 0;
    out->undetectable_gap = (in->cnt_bits >= 8u)
                          ? 256u : (1u << in->cnt_bits);
    return 0;
}
