/* c/P05/cobs.c: consistent overhead byte stuffing.
 *
 * WRITTEN HERE RATHER THAN VENDORED, and the reason is worth stating. The chapter
 * names Craig McQueen's MIT-licensed implementation as the best of its kind and
 * says to vendor it with its copyright header kept. Vendoring means fetching that
 * source and carrying its header, not reproducing it from recollection, so until
 * it is actually fetched this is an implementation written from the format's
 * definition. If it is later replaced by the real thing, that is a visible commit
 * and the header comes with it.
 *
 * WHY BYTE STUFFING AND NOT A LENGTH PREFIX. A length prefix is cheap and
 * obvious, and it fails badly: corrupt the length and the receiver skips an
 * arbitrary number of bytes, so one bad byte costs several frames and there is no
 * reliable way back into step. Stuffing gives a byte value that cannot appear
 * inside a frame, so a receiver that has lost its place recovers at the next
 * delimiter, always, after losing exactly one frame.
 *
 * The cost is bounded and small: one overhead byte per 254 bytes of payload, plus
 * one. For the five-byte payload of P09 that is one byte.
 *
 * Builds unchanged on the host and on the target.
 */
#include "cobs.h"

size_t cobs_encode_max(size_t len)
{
    /* One code byte per run of up to 254, plus one for the first run. */
    return len + (len / 254u) + 1u;
}

size_t cobs_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    if (src == NULL || dst == NULL || cap < cobs_encode_max(len)) {
        return 0;
    }

    uint8_t *const start = dst;
    uint8_t       *code_at = dst++;     /* the code byte for the run in progress */
    uint8_t        code = 1u;           /* counts itself */

    for (size_t i = 0; i < len; i++) {
        if (src[i] != 0u) {
            *dst++ = src[i];
            code++;
        }
        /* A zero in the input ends the run and is represented by the code rather
         * than written. A run of 254 non-zero bytes also ends, because the code
         * byte cannot count higher. */
        if (src[i] == 0u || code == 0xFFu) {
            *code_at = code;
            code_at = dst++;
            code = 1u;
        }
    }
    *code_at = code;

    return (size_t) (dst - start);
}

size_t cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    if (src == NULL || dst == NULL) {
        return 0;
    }

    const uint8_t *const end = src + len;
    uint8_t *const start = dst;

    while (src < end) {
        const uint8_t code = *src++;

        /* A zero byte cannot appear inside encoded data: it is the delimiter and
         * the caller should have split on it. Finding one means the frame is
         * corrupt or the caller passed the delimiter in, and both are refusals
         * rather than something to interpret. */
        if (code == 0u) {
            return 0;
        }

        for (uint8_t i = 1u; i < code; i++) {
            /* Truncated frame: the code promised more bytes than arrived. This is
             * the common shape of a corrupted frame and refusing it is the whole
             * point of checking. */
            if (src >= end) {
                return 0;
            }
            if ((size_t) (dst - start) >= cap) {
                return 0;
            }
            *dst++ = *src++;
        }

        /* A run shorter than 254 ended on a zero in the original data, so put it
         * back. A run of exactly 254 did not, and a run at the very end of the
         * frame did not either. */
        if (code != 0xFFu && src < end) {
            if ((size_t) (dst - start) >= cap) {
                return 0;
            }
            *dst++ = 0u;
        }
    }

    return (size_t) (dst - start);
}
