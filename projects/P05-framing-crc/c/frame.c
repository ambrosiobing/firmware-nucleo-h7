/* projects/P05-framing-crc/c/frame.c: the frame, with a stated reason for every field.
 *
 * ON THE WIRE, for a payload of n bytes:
 *
 *     [ COBS( payload || crc16(payload) ) ] [ 0x00 ]
 *                                              delimiter
 *
 * The checksum is two bytes, most significant first. It covers the payload and
 * not itself, and not the stuffing: stuffing is a transport concern and a
 * checksum over the stuffed form would have to be recomputed by anything that
 * restuffed, which nothing should need to do.
 *
 * For P09's five-byte payload that is 5 + 2 = 7 bytes into COBS, which is 8, plus
 * the delimiter: nine bytes on the wire. That nine is the number P09's argument
 * for bit packing rests on, so it is worth being able to derive it.
 *
 * WHY THE ORDER IS CHECKSUM INSIDE THE STUFFING. Stuffing outside means the
 * delimiter cannot appear anywhere in the frame including the checksum, so a
 * receiver that has lost its place always recovers at the next delimiter. If the
 * checksum were outside the stuffing it could contain a zero byte and the
 * delimiter would stop being unambiguous, which is the one property the whole
 * format is chosen for.
 *
 * WHY NOT A LENGTH PREFIX. Corrupt the length and the receiver skips an arbitrary
 * number of bytes, so a single bad byte costs several frames and there is no
 * reliable way back into step. With stuffing, one bad byte costs exactly one
 * frame.
 *
 * Builds unchanged on the host and on the target. No target headers.
 */
#include <string.h>

#include "frame.h"

#include "cobs.h"
#include "crc16.h"

size_t frame_encode_max(size_t payload_len)
{
    return cobs_encode_max(payload_len + FRAME_CRC_BYTES) + 1u;  /* + delimiter */
}

size_t frame_encode(const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
    if (payload == NULL || out == NULL || len > FRAME_MAX_PAYLOAD) {
        return 0;
    }
    if (cap < frame_encode_max(len)) {
        return 0;
    }

    /* Payload then checksum, as one buffer, so the stuffing covers both. */
    uint8_t body[FRAME_MAX_PAYLOAD + FRAME_CRC_BYTES];
    memcpy(body, payload, len);

    const uint16_t crc = crc16(payload, len);
    body[len]     = (uint8_t) (crc >> 8);     /* most significant first */
    body[len + 1] = (uint8_t) (crc & 0xFFu);

    const size_t stuffed = cobs_encode(body, len + FRAME_CRC_BYTES, out, cap - 1u);
    if (stuffed == 0u) {
        return 0;
    }

    out[stuffed] = FRAME_DELIMITER;
    return stuffed + 1u;
}

frame_result_t frame_decode(const uint8_t *stuffed, size_t len,
                            uint8_t *payload, size_t cap, size_t *payload_len)
{
    if (stuffed == NULL || payload == NULL || payload_len == NULL) {
        return FRAME_ERR_ARGS;
    }
    *payload_len = 0u;

    /* The caller splits on the delimiter and passes what lies between, so an
     * empty run between two delimiters is not an error: it is what a line that
     * has been idle looks like, and it is discarded rather than reported. */
    if (len == 0u) {
        return FRAME_EMPTY;
    }

    uint8_t body[FRAME_MAX_PAYLOAD + FRAME_CRC_BYTES];
    const size_t body_len = cobs_decode(stuffed, len, body, sizeof body);
    if (body_len == 0u) {
        return FRAME_ERR_STUFFING;
    }

    /* A frame shorter than its own checksum cannot be checked, and treating the
     * checksum bytes as payload would be the worst available answer: it would
     * produce a plausible short frame from rubbish. */
    if (body_len < FRAME_CRC_BYTES + 1u) {
        return FRAME_ERR_TOO_SHORT;
    }

    const size_t n = body_len - FRAME_CRC_BYTES;
    const uint16_t got = (uint16_t) (((uint16_t) body[n] << 8) | body[n + 1]);
    const uint16_t want = crc16(body, n);

    if (got != want) {
        /* Discarded, and counted by the caller. A frame that fails its checksum
         * is never partially delivered: half a frame that looks like data is how
         * a corrupt link produces a confident wrong reading. */
        return FRAME_ERR_CHECKSUM;
    }

    if (n > cap) {
        return FRAME_ERR_TOO_LONG;
    }

    memcpy(payload, body, n);
    *payload_len = n;
    return FRAME_OK;
}
