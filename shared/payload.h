/* codec/payload.h: the whole interface, deliberately two functions and a buffer.
 *
 * Keeping it this narrow is what lets the same encoder be called from the
 * board, from a host test through a foreign function interface, and from the
 * C++ variant without change. Anything that widens it, an allocator, a logging
 * call, a dependency on the node context of chapter 8, costs this chapter its
 * best property: that the codec can be tested completely with no hardware.
 *
 * The codec touches no peripheral and holds no state.
 */
#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#include "payload_fields.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t version;    /* PAYLOAD_W_VERSION  bits, unsigned */
    uint32_t flags;      /* PAYLOAD_W_FLAGS    bits, unsigned */
    uint32_t sequence;   /* PAYLOAD_W_SEQUENCE bits, unsigned */
    int32_t  feature;    /* PAYLOAD_W_FEATURE  bits, two's complement */
    uint32_t battery;    /* PAYLOAD_W_BATTERY  bits, unsigned */
} payload_t;

/* Encode into out. Returns PAYLOAD_BYTES on success and 0 on failure, which
 * happens only when cap is too small. Out-of-range field values are masked to
 * their width rather than rejected, which is the documented behaviour: a
 * sequence counter that wraps at 512 is meant to wrap, not to fail. */
size_t payload_encode(uint8_t *out, size_t cap, const payload_t *p);

/* Decode exactly PAYLOAD_BYTES bytes. Returns 0 on success and -1 when len is
 * wrong. Sign extension of the signed field is explicit, which is the one
 * thing a bit-packed decoder gets wrong, because it works for every positive
 * test value. */
int payload_decode(const uint8_t *in, size_t len, payload_t *p);

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_H */
