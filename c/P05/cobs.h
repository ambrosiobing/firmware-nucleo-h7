/* c/P05/cobs.h: consistent overhead byte stuffing, host and target.
 *
 * The delimiter is 0x00 and it cannot appear inside an encoded frame, which is
 * the whole property: a receiver that has lost its place recovers at the next
 * delimiter after losing exactly one frame. A length prefix cannot promise that.
 *
 * Every function returns 0 on failure rather than a partial result, and the
 * caller must check. A partial decode that looks like a short frame is how a
 * corrupt frame becomes plausible data.
 */
#ifndef COBS_H
#define COBS_H

#include <stddef.h>
#include <stdint.h>

/* The largest encoding of len bytes: one code byte per 254, plus one. */
size_t cobs_encode_max(size_t len);

/* Returns the encoded length, or 0 if cap is too small. The delimiter is NOT
 * appended: framing owns that, because the delimiter belongs to the stream
 * rather than to the encoding. */
size_t cobs_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap);

/* Returns the decoded length, or 0 on a corrupt frame: a zero byte inside the
 * data, a code promising more bytes than arrived, or an overrun of cap. */
size_t cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap);

#endif /* COBS_H */
