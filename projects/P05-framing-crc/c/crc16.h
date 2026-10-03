/* projects/P05-framing-crc/c/crc16.h: the 16-bit checksum, software reference and peripheral.
 *
 * Two implementations behind one interface, selected with -DCRC_IMPL=ref|hw, so
 * the comparison is of mechanisms rather than of two programs. The reference is
 * the arbiter: it builds unchanged on the host and on the target, and the
 * peripheral's answer is only ever believed when it matches.
 *
 * THE PARAMETERS, and all six matter.
 *
 *   polynomial        0x1021
 *   initial value     0xFFFF
 *   input reflected   no
 *   output reflected  no
 *   final exclusive-or  none
 *   width             16
 *
 * The first two are the ones everybody states. The reversal settings are the ones
 * that go wrong, because a checksum computed with the input bits reflected is a
 * perfectly good checksum that disagrees with every published catalogue and with
 * whatever is at the other end of the link. The peripheral on this part can be
 * configured either way, and the default is not necessarily the one above.
 *
 * THE ONE LINE THAT MAKES THIS CHECKABLE. The published check value of these
 * parameters over the nine bytes "123456789" is 0x29B1. Every catalogue of
 * these polynomials lists it. If an implementation does not produce that, nothing
 * else in this project is worth doing, and the test asserts it first.
 */
#ifndef CRC16_H
#define CRC16_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The published check value over the nine bytes "123456789". */
#define CRC16_CHECK_VALUE 0x29B1u
#define CRC16_INIT        0xFFFFu
#define CRC16_POLY        0x1021u

/* The software reference. Bitwise, deliberately: it is the arbiter and being
 * obviously correct matters more here than being fast. A table version belongs
 * in the measurement, not in the reference. */
uint16_t crc16_ref(const uint8_t *data, size_t len);

/* Whichever implementation this build selected. On the host that is always the
 * reference; on the target it may be the peripheral. */
uint16_t crc16(const uint8_t *data, size_t len);

/* Which one, for the status line and the results table. */
const char *crc16_name(void);

/* The peripheral, when built for the target with -DCRC_IMPL=hw. Returns 0 and
 * sets *ok to false while its configuration is unconfirmed against RM0455,
 * rather than returning a plausible wrong checksum. A wrong checksum that looks
 * right is the worst outcome available here: both ends agree with each other and
 * with nothing else. */
uint16_t crc16_hw(const uint8_t *data, size_t len, bool *ok);

#endif /* CRC16_H */
