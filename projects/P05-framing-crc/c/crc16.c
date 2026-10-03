/* projects/P05-framing-crc/c/crc16.c: the software reference, which is the arbiter for everything else.
 *
 * Bitwise rather than table driven, on purpose. This function decides whether the
 * peripheral is configured correctly, so being obviously correct from the
 * parameters matters more than being fast. A table is a derived artefact and
 * belongs in the measurement, where its speed is the point.
 *
 * Builds unchanged on the host and on the target. No target headers, no
 * conditional compilation.
 */
#include "crc16.h"

uint16_t crc16_ref(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_INIT;

    for (size_t i = 0; i < len; i++) {
        /* No input reflection: the byte goes into the high half as it is. A
         * reflected variant would shift it in at the bottom and walk the other
         * way, and would produce a checksum that is internally consistent and
         * agrees with nothing published. */
        crc ^= (uint16_t) ((uint16_t) data[i] << 8);

        for (unsigned bit = 0; bit < 8u; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t) ((uint16_t) (crc << 1) ^ CRC16_POLY);
            } else {
                crc = (uint16_t) (crc << 1);
            }
        }
    }

    /* No final exclusive-or, and no output reflection. */
    return crc;
}

/* The selected implementation. The host has only one; the target may have two. */
#if defined(CRC_IMPL_HW)

uint16_t crc16(const uint8_t *data, size_t len)
{
    bool ok = false;
    const uint16_t v = crc16_hw(data, len, &ok);
    /* Falling back to the reference silently would hide a misconfigured
     * peripheral behind a correct answer, which is precisely the defect this
     * project exists to catch. Returning the reference value here would make the
     * agreement test pass while the peripheral was wrong. */
    return ok ? v : 0u;
}

const char *crc16_name(void) { return "hardware peripheral"; }

#else

uint16_t crc16(const uint8_t *data, size_t len) { return crc16_ref(data, len); }
const char *crc16_name(void) { return "software reference"; }

#endif
