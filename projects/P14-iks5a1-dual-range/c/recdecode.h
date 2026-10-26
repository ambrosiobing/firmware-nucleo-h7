/* recdecode.h: chapter 14's record, its scale codes, and what they cost.
 *
 * WHAT THIS IS. The host half of chapter 14 steps 7, 9 and 10, which the chapter
 * itself says to write at the same time as the firmware: "a format with no second
 * implementation is a format with no specification". The record header, the
 * conversion of counts to engineering units THROUGH THE CODES IN THE RECORD, the
 * barometric relation, and the drain time step 10 says to compute before looking
 * for it. None of it needs the shield.
 *
 * THE SCALE CODES ARE THE POINT OF THE FORMAT. Step 9 is explicit: the decoder
 * converts "using the scale codes in the header rather than a constant compiled
 * into the script", and "keeping the scale in the record is what lets a record
 * recorded under one configuration be read years later". So an unrecognised code
 * is refused here rather than defaulted. A decoder that silently assumes a scale
 * is exactly the compiled constant the format exists to remove, and it would
 * produce a plausible number from a record it could not read.
 *
 * THE HEADER IS PARSED BYTE BY BYTE AND NOT CAST. The chapter declares a packed
 * structure for the firmware to write, which is right there. Casting a byte
 * buffer to that structure on a host assumes the host's byte order and its
 * tolerance for unaligned members, and the whole value of a second
 * implementation is that it shares no assumption with the first. The field order
 * and the little-endian byte order are the contract, stated here and parsed
 * explicitly.
 *
 * FINDING ONE: THE EVENT WINDOW SELECTS THE SCALE WITH THE WORST ALTITUDE
 * RESOLUTION. Step 6 selects the narrow barometer scale for the quiet state,
 * "where the finer sensitivity is what gives altitude any resolution", and the
 * wide scale "inside an event window". Altitude resolution is the pressure
 * resolution times about 8.3 metres per hectopascal, so it degrades by exactly
 * the ratio of the two sensitivities, and it does so during the event the
 * recorder exists to capture. rec_altitude_resolution_m() reports it per range
 * so the number travels with the trace. Whether that is the right trade depends
 * on whether the event moves the pressure past the narrow scale's end, which is
 * a measurement nobody here has made; what is wrong is leaving it unstated.
 *
 * FINDING TWO: THE ALTITUDE IS DIFFERENTIAL AND THE REFERENCE IS NOT IN THE
 * RECORD. Step 9's relation needs p0, and the header carries the two
 * acceleration scales, the barometer range, the tick and the counts, but no
 * reference pressure. With a sea-level constant the answer carries the day's
 * weather: at about 8.3 metres per hectopascal a 10 hPa deviation is 83 metres
 * of altitude that is not there. The experiment in step 8 squeezes a bag, where
 * only the difference matters, so the reference belongs in the record beside the
 * scales for the same reason the scales do. rec_altitude_m() therefore takes p0
 * as an argument and refuses a non-positive one rather than carrying a default.
 *
 * FINDING THREE, AND IT IS SMALL: STEP 10'S FIGURE OMITS THE OVERHEAD ITS OWN
 * COMMENT NAMES. "128 samples of 6 bytes is 768 bytes, plus address and register
 * overhead. At 400 kHz each byte costs 9 bit times, so 768 x 9 / 400000 =
 * 17.3 ms." The arithmetic is correct and the overhead is then not in it. Two
 * more bytes at nine bit times each is 0.045 ms, about a quarter of one per
 * cent, so this changes no conclusion; it is recorded because the comment and
 * the sum disagree and a reader cannot tell which was meant.
 *
 * WHAT IS NOT HERE. Both acceleration channels live, the watermark line, the
 * pre-trigger ring, the arming on the high-g line, the cost of changing the
 * barometer's full scale, the foam-pad event, the sealed bag, and the rate at
 * which the bus stops keeping up. Those need the X-NUCLEO-IKS5A1, which the
 * inventory does not hold. There is no measured number in this project.
 */
#ifndef RECDECODE_H
#define RECDECODE_H

#include <stdint.h>

#define REC_HDR_BYTES 14u

/* The acceleration full scales this format can express, as the codes the
 * firmware writes. A code outside the table is refused. */
#define REC_FS_CODES 5u

/* The barometer ranges of step 6, as the record's one-byte code. */
#define REC_BARO_NARROW 0u
#define REC_BARO_WIDE   1u
#define REC_BARO_CODES  2u

/* The flags of step 7, bit for bit. */
#define REC_FLAG_SHORT_WINDOW 0x01u
#define REC_FLAG_EXTENDED     0x02u
#define REC_FLAG_BARO_BLANKED 0x04u
#define REC_FLAG_KNOWN        0x07u

typedef struct {
    uint32_t tick_event;
    uint16_t n_pre;
    uint16_t n_post;
    uint16_t n_baro;
    uint8_t lo_fs_g;
    uint8_t hi_fs_g;
    uint8_t baro_range;
    uint8_t flags;
} rec_hdr_t;

/* The code-to-sensitivity table, supplied by the caller rather than compiled in
 * here, and the reason is the same one the chapter gives for putting codes in
 * the record. The sensitivities belong to the sensors and are in their
 * datasheets, which this bench does not hold; inventing them in a header would
 * be the compiled constant step 9 exists to remove, one level further down. A
 * code whose valid bit is clear is refused, so a table that does not cover a
 * record cannot silently decode it. */
typedef struct {
    double lo_mg_per_lsb[REC_FS_CODES];
    double hi_mg_per_lsb[REC_FS_CODES];
    double baro_lsb_per_hpa[REC_BARO_CODES];
    uint8_t lo_valid;    /* one bit per code this table defines              */
    uint8_t hi_valid;
    uint8_t baro_valid;
} rec_table_t;

typedef struct {
    double lo_mg_per_lsb;   /* from the record's lo_fs_g code                */
    double hi_mg_per_lsb;   /* from the record's hi_fs_g code                */
    double baro_lsb_per_hpa;/* from the record's baro_range code             */
} rec_scales_t;

#define REC_ERR_ARGUMENT (-1) /* a null pointer or a short buffer            */
#define REC_ERR_FS_CODE  (-2) /* an acceleration scale code not in the table */
#define REC_ERR_BARO     (-3) /* a barometer range code not in the table     */
#define REC_ERR_FLAGS    (-4) /* an unknown flag bit, or two that contradict */
#define REC_ERR_COUNTS   (-5) /* a sample count past what the record can hold */
#define REC_ERR_PRESSURE (-6) /* a pressure or reference at or below zero    */

/* Parse the header from its bytes, little endian, in the field order above.
 * Refuses rather than defaults on every code it does not recognise. */
int rec_parse_header(const uint8_t *buf, uint32_t len, rec_hdr_t *out);

/* The scales the record asks for, looked up from its own codes in the caller's
 * table. */
int rec_scales(const rec_hdr_t *hdr, const rec_table_t *table,
               rec_scales_t *out);

/* Counts to milli-g, through the scale the record names. fine selects the fine
 * channel when non-zero and the coarse one when zero, because both are running
 * the whole time and a record carries samples from each. */
int rec_counts_to_mg(const rec_hdr_t *hdr, const rec_table_t *table,
                     int16_t counts, int fine, double *out);

/* Counts to hectopascals, through the range the record names. */
int rec_counts_to_hpa(const rec_hdr_t *hdr, const rec_table_t *table,
                      int32_t counts, double *out);

/* Step 9's relation exactly, with the reference as an argument because the
 * record does not carry one. Refuses a non-positive pressure or reference. */
int rec_altitude_m(double p_hpa, double p0_hpa, double *out);

/* Metres per least significant bit of pressure at a stated pressure, which is
 * what finding one is about. */
int rec_altitude_resolution_m(double lsb_per_hpa, double p_hpa, double p0_hpa,
                              double *out);

/* Step 10's drain time. overhead_bytes is the address and register cost the
 * chapter's comment names and its sum omits; pass 0 to reproduce the printed
 * figure and 2 for the transaction the comment describes. */
int rec_drain_ms(uint32_t samples, uint32_t bytes_per_sample,
                 uint32_t overhead_bytes, double bus_hz, double *out);

#endif /* RECDECODE_H */
