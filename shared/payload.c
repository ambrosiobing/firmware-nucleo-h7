/* codec/payload.c: MSB first, no padding between fields.
 *
 * Twenty-odd lines of bit shuffling, written rather than depended on, because
 * every dependency that could replace it brings either a different language or
 * a different wire format. docs/bitorder.md is normative for everything here.
 */
#include <string.h>

#include "payload.h"

/* ------------------------------------------------------------------ writing */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   bit;
} bw_t;

static int bw_put(bw_t *w, uint32_t value, unsigned width)
{
    if (w->bit + width > w->cap * 8u) {
        return -1;
    }
    for (unsigned i = 0; i < width; i++) {
        unsigned src  = width - 1u - i;            /* MSB of the field first */
        size_t   dst  = w->bit + i;
        uint8_t  bit  = (uint8_t) ((value >> src) & 1u);
        uint8_t  mask = (uint8_t) (0x80u >> (dst & 7u));
        if (bit) {
            w->buf[dst >> 3] |= mask;
        } else {
            w->buf[dst >> 3] = (uint8_t) (w->buf[dst >> 3] & (uint8_t) ~mask);
        }
    }
    w->bit += width;
    return 0;
}

size_t payload_encode(uint8_t *out, size_t cap, const payload_t *p)
{
    if (out == NULL || p == NULL || cap < PAYLOAD_BYTES) {
        return 0;
    }
    memset(out, 0, PAYLOAD_BYTES);

    bw_t w = { out, cap, 0u };

    /* Every field is masked to its width on the way in. For the signed field
     * this is the line that decides whether the codec is portable: a negative
     * int32_t shifted right is implementation defined in C, so the value is
     * converted to unsigned, where the conversion is defined to be modulo
     * 2^32, and then masked. Nothing here shifts a signed value. */
    if (bw_put(&w, p->version  & PAYLOAD_M_VERSION,  PAYLOAD_W_VERSION)  != 0) return 0;
    if (bw_put(&w, p->flags    & PAYLOAD_M_FLAGS,    PAYLOAD_W_FLAGS)    != 0) return 0;
    if (bw_put(&w, p->sequence & PAYLOAD_M_SEQUENCE, PAYLOAD_W_SEQUENCE) != 0) return 0;
    if (bw_put(&w, (uint32_t) p->feature & PAYLOAD_M_FEATURE,
                                             PAYLOAD_W_FEATURE)  != 0) return 0;
    if (bw_put(&w, p->battery  & PAYLOAD_M_BATTERY,  PAYLOAD_W_BATTERY)  != 0) return 0;

    return PAYLOAD_BYTES;
}

/* ------------------------------------------------------------------ reading */

static uint32_t br_get(const uint8_t *in, size_t start, unsigned width)
{
    uint32_t v = 0u;
    for (unsigned i = 0; i < width; i++) {
        size_t b = start + i;
        v = (v << 1) | (uint32_t) ((in[b >> 3] >> (7u - (b & 7u))) & 1u);
    }
    return v;
}

/* Sign extension, written out rather than done with a shift, for the same
 * reason the encoder masks: the shift would be the implementation-defined one.
 *
 * The subtraction is done in signed arithmetic on two values that both fit in
 * an int32_t, which is the detail that matters. Writing it as
 * (int32_t)(v - (1u << width)) looks tidier and is wrong: the unsigned
 * subtraction wraps, and converting an out-of-range unsigned value to a signed
 * type is implementation defined before C23. That is the same class of defect
 * as the arithmetic shift this function exists to avoid, so it is avoided here
 * too rather than traded for.
 *
 * This holds for any width from 2 to 31. A 32-bit signed field would need a
 * different formulation, and the specification asserts the total is 40 bits so
 * no field can reach that. */
static int32_t sign_extend(uint32_t v, unsigned width)
{
    const uint32_t sign_bit = 1u << (width - 1u);
    if (v & sign_bit) {
        return (int32_t) v - (int32_t) (sign_bit << 1);
    }
    return (int32_t) v;
}

int payload_decode(const uint8_t *in, size_t len, payload_t *p)
{
    if (in == NULL || p == NULL || len != PAYLOAD_BYTES) {
        return -1;
    }
    p->version  = br_get(in, PAYLOAD_O_VERSION,  PAYLOAD_W_VERSION);
    p->flags    = br_get(in, PAYLOAD_O_FLAGS,    PAYLOAD_W_FLAGS);
    p->sequence = br_get(in, PAYLOAD_O_SEQUENCE, PAYLOAD_W_SEQUENCE);
    p->feature  = sign_extend(br_get(in, PAYLOAD_O_FEATURE, PAYLOAD_W_FEATURE),
                              PAYLOAD_W_FEATURE);
    p->battery  = br_get(in, PAYLOAD_O_BATTERY,  PAYLOAD_W_BATTERY);
    return 0;
}
