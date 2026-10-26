/* recdecode.c: parse the record, convert through its own codes, price the bus.
 *
 * The header says at length why the sensitivities are a caller's table rather
 * than constants here, why the header is parsed rather than cast, and what the
 * three findings are.
 */
#include "recdecode.h"

#include <math.h>
#include <stddef.h>

/* Step 9's exponent and scale factor, as the chapter prints them. They are the
 * standard atmosphere's, 288.15 K over a lapse rate of 6.5 K per kilometre and
 * the exponent R L / g M, and they are left exactly as written so that a
 * disagreement with the chapter is visible rather than absorbed. */
#define REC_ALT_SCALE 44330.0
#define REC_ALT_EXPONENT 0.1903

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

int rec_parse_header(const uint8_t *buf, uint32_t len, rec_hdr_t *out)
{
    if (buf == NULL || out == NULL) {
        return REC_ERR_ARGUMENT;
    }
    if (len < REC_HDR_BYTES) {
        return REC_ERR_ARGUMENT;
    }

    out->tick_event = le32(&buf[0]);
    out->n_pre = le16(&buf[4]);
    out->n_post = le16(&buf[6]);
    out->n_baro = le16(&buf[8]);
    out->lo_fs_g = buf[10];
    out->hi_fs_g = buf[11];
    out->baro_range = buf[12];
    out->flags = buf[13];

    if (out->lo_fs_g >= REC_FS_CODES || out->hi_fs_g >= REC_FS_CODES) {
        return REC_ERR_FS_CODE;
    }
    if (out->baro_range >= REC_BARO_CODES) {
        return REC_ERR_BARO;
    }
    if ((out->flags & ~REC_FLAG_KNOWN) != 0u) {
        return REC_ERR_FLAGS;
    }
    /* A window cannot be both shorter than asked for and extended beyond it.
     * The chapter defines the two bits separately and never says they are
     * exclusive, which is the kind of thing a second implementation is for. */
    if ((out->flags & REC_FLAG_SHORT_WINDOW) != 0u
        && (out->flags & REC_FLAG_EXTENDED) != 0u) {
        return REC_ERR_FLAGS;
    }
    /* A record claiming no samples at all on either acceleration channel is a
     * header without a record, and a pressure count of zero with the barometer
     * not blanked is the same thing for the other channel. */
    if (out->n_pre == 0u && out->n_post == 0u) {
        return REC_ERR_COUNTS;
    }
    if (out->n_baro == 0u && (out->flags & REC_FLAG_BARO_BLANKED) == 0u) {
        return REC_ERR_COUNTS;
    }
    return 0;
}

int rec_scales(const rec_hdr_t *hdr, const rec_table_t *table,
               rec_scales_t *out)
{
    if (hdr == NULL || table == NULL || out == NULL) {
        return REC_ERR_ARGUMENT;
    }
    if (hdr->lo_fs_g >= REC_FS_CODES || hdr->hi_fs_g >= REC_FS_CODES) {
        return REC_ERR_FS_CODE;
    }
    if (hdr->baro_range >= REC_BARO_CODES) {
        return REC_ERR_BARO;
    }
    if (((table->lo_valid >> hdr->lo_fs_g) & 1u) == 0u
        || ((table->hi_valid >> hdr->hi_fs_g) & 1u) == 0u) {
        return REC_ERR_FS_CODE;
    }
    if (((table->baro_valid >> hdr->baro_range) & 1u) == 0u) {
        return REC_ERR_BARO;
    }

    out->lo_mg_per_lsb = table->lo_mg_per_lsb[hdr->lo_fs_g];
    out->hi_mg_per_lsb = table->hi_mg_per_lsb[hdr->hi_fs_g];
    out->baro_lsb_per_hpa = table->baro_lsb_per_hpa[hdr->baro_range];
    if (!(out->lo_mg_per_lsb > 0.0) || !(out->hi_mg_per_lsb > 0.0)) {
        return REC_ERR_FS_CODE;
    }
    if (!(out->baro_lsb_per_hpa > 0.0)) {
        return REC_ERR_BARO;
    }
    return 0;
}

int rec_counts_to_mg(const rec_hdr_t *hdr, const rec_table_t *table,
                     int16_t counts, int fine, double *out)
{
    if (out == NULL) {
        return REC_ERR_ARGUMENT;
    }
    rec_scales_t s;
    const int bad = rec_scales(hdr, table, &s);
    if (bad != 0) {
        return bad;
    }
    *out = (double) counts * ((fine != 0) ? s.lo_mg_per_lsb
                                          : s.hi_mg_per_lsb);
    return 0;
}

int rec_counts_to_hpa(const rec_hdr_t *hdr, const rec_table_t *table,
                      int32_t counts, double *out)
{
    if (out == NULL) {
        return REC_ERR_ARGUMENT;
    }
    rec_scales_t s;
    const int bad = rec_scales(hdr, table, &s);
    if (bad != 0) {
        return bad;
    }
    *out = (double) counts / s.baro_lsb_per_hpa;
    return 0;
}

int rec_altitude_m(double p_hpa, double p0_hpa, double *out)
{
    if (out == NULL) {
        return REC_ERR_ARGUMENT;
    }
    /* The reference is an argument because the record does not carry one, and a
     * default here would be the compiled constant this project is about. */
    if (!(p_hpa > 0.0) || !(p0_hpa > 0.0)) {
        return REC_ERR_PRESSURE;
    }
    *out = REC_ALT_SCALE * (1.0 - pow(p_hpa / p0_hpa, REC_ALT_EXPONENT));
    return 0;
}

int rec_altitude_resolution_m(double lsb_per_hpa, double p_hpa, double p0_hpa,
                              double *out)
{
    if (out == NULL) {
        return REC_ERR_ARGUMENT;
    }
    if (!(lsb_per_hpa > 0.0) || !(p_hpa > 0.0) || !(p0_hpa > 0.0)) {
        return REC_ERR_PRESSURE;
    }
    /* The derivative of step 9's relation, times one least significant bit.
     * Taken analytically rather than by differencing two altitudes, because a
     * difference of two nearly equal altitudes is where the resolution of this
     * very calculation would show up as noise. */
    const double slope = REC_ALT_SCALE * REC_ALT_EXPONENT / p0_hpa
                       * pow(p_hpa / p0_hpa, REC_ALT_EXPONENT - 1.0);
    *out = slope / lsb_per_hpa;
    return 0;
}

int rec_drain_ms(uint32_t samples, uint32_t bytes_per_sample,
                 uint32_t overhead_bytes, double bus_hz, double *out)
{
    if (out == NULL) {
        return REC_ERR_ARGUMENT;
    }
    if (samples == 0u || bytes_per_sample == 0u) {
        return REC_ERR_COUNTS;
    }
    if (!(bus_hz > 0.0)) {
        return REC_ERR_PRESSURE;
    }
    /* Nine bit times a byte, which is the eight bits and the acknowledgement,
     * exactly as the chapter's comment states it. */
    const double bytes = (double) samples * (double) bytes_per_sample
                       + (double) overhead_bytes;
    *out = 1000.0 * (bytes * 9.0) / bus_hz;
    return 0;
}
