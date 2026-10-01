/* c/P05/frame.h: the frame format, and what every decode can return.
 *
 * On the wire:  [ COBS( payload || crc16(payload) ) ] [ 0x00 ]
 *
 * The caller splits the stream on the delimiter and hands frame_decode what lies
 * between. That split is the receiver's job because it belongs to the stream, not
 * to the frame, and P03's ring is where the bytes come from.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdint.h>

#define FRAME_DELIMITER   0x00u
#define FRAME_CRC_BYTES   2u
#define FRAME_MAX_PAYLOAD 64u

/* Every way a decode can end. Separate codes rather than one failure, because the
 * counts are different findings: stuffing errors mean the receiver lost its place,
 * checksum errors mean the line corrupted a byte, and a reader who sees only a
 * total cannot tell a noisy cable from a desynchronised receiver. */
typedef enum {
    FRAME_OK             =  0,
    FRAME_EMPTY          =  1,   /* nothing between two delimiters; an idle line */
    FRAME_ERR_ARGS       = -1,
    FRAME_ERR_STUFFING   = -2,   /* truncated, or a zero inside the data */
    FRAME_ERR_TOO_SHORT  = -3,   /* shorter than its own checksum */
    FRAME_ERR_TOO_LONG   = -4,   /* payload longer than the caller's buffer */
    FRAME_ERR_CHECKSUM   = -5,   /* the line corrupted at least one byte */
} frame_result_t;

/* The largest frame a payload of this length can produce, delimiter included. */
size_t frame_encode_max(size_t payload_len);

/* Returns the frame length including the delimiter, or 0 on failure. */
size_t frame_encode(const uint8_t *payload, size_t len, uint8_t *out, size_t cap);

/* Decodes what lay between two delimiters. On FRAME_OK, *payload_len is set and
 * the payload is written. On anything else *payload_len is 0 and nothing is
 * written: a frame that fails is never partially delivered. */
frame_result_t frame_decode(const uint8_t *stuffed, size_t len,
                            uint8_t *payload, size_t cap, size_t *payload_len);

#endif /* FRAME_H */
