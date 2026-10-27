/* features.c: the three pinned feature groups and the bounded tree walk.
 *
 * Every rounding here truncates toward negative infinity and the header says
 * why: step 4's gate demands equality between three implementations, and a
 * rounding the definition leaves open is a disagreement the gate will find.
 */
#include "features.h"

#include <stddef.h>

/* Floor division by a power of two, written out rather than as a shift, because
 * a right shift of a negative signed value is implementation defined in C and
 * the direction of this rounding is the thing being pinned. */
static int32_t floor_div_pow2(int32_t v, uint32_t bits)
{
    const int32_t d = (int32_t) 1 << bits;
    int32_t q = v / d;
    if (v < 0 && (v % d) != 0) {
        q -= 1;
    }
    return q;
}

/* The integer square root, truncated: the largest r with r*r <= v. Written as a
 * bit-by-bit descent rather than with a floating-point sqrt, so the result is
 * the same on every implementation and on a part with no floating-point unit.
 * A float sqrt followed by a cast would agree almost always, and almost always
 * is what an exact gate exists to refuse. */
static uint32_t isqrt_floor(uint64_t v)
{
    uint64_t rest = v;
    uint64_t root = 0u;
    /* The highest even power of four not above the input, for a 64-bit value. */
    uint64_t bit = (uint64_t) 1 << 62;
    while (bit > rest) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (rest >= root + bit) {
            rest -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t) root;
}

int feat_window(const int16_t *samples, uint32_t n, uint32_t axes,
                feat_t *out)
{
    if (samples == NULL || out == NULL) {
        return FEAT_ERR_ARGUMENT;
    }
    if (n == 0u) {
        return FEAT_ERR_ARGUMENT;
    }
    /* Refused rather than scaled. A feature computed over a different window
     * length is a different feature, and step 3 fixes the length. */
    if (n != FEAT_WIN_N || axes != FEAT_AXES) {
        return FEAT_ERR_SHAPE;
    }

    for (uint32_t a = 0u; a < FEAT_AXES; a++) {
        int32_t sum = 0;
        uint64_t squares = 0u;
        int32_t lowest = 32767;
        int32_t highest = -32768;

        for (uint32_t i = 0u; i < FEAT_WIN_N; i++) {
            const int32_t v = samples[i * FEAT_AXES + a];
            sum += v;
            squares += (uint64_t) ((int64_t) v * (int64_t) v);
            if (v < lowest) {
                lowest = v;
            }
            if (v > highest) {
                highest = v;
            }
        }

        /* 128 is two to the seventh, so the mean is an exact shift with a
         * stated direction rather than a division with an open one. */
        out->mean[a] = (int16_t) floor_div_pow2(sum, 7u);

        /* The mean of the squares, floored, then its integer square root, also
         * floored. Two truncations, both named. */
        const uint64_t mean_square = squares >> 7u;
        out->rms[a] = (int16_t) isqrt_floor(mean_square);

        /* Peak to peak can reach 65535, which Q15 cannot hold, so it saturates
         * and the README says so. A wrapped peak to peak would read as a quiet
         * window, which is the most misleading answer available here. */
        const int32_t span = highest - lowest;
        out->p2p[a] = (span > 32767) ? (int16_t) 32767 : (int16_t) span;
    }
    return 0;
}

int feat_vector(const feat_t *in, int16_t *out, uint32_t count)
{
    if (in == NULL || out == NULL) {
        return FEAT_ERR_ARGUMENT;
    }
    if (count != FEAT_COUNT) {
        return FEAT_ERR_SHAPE;
    }
    for (uint32_t a = 0u; a < FEAT_AXES; a++) {
        out[a] = in->mean[a];
        out[FEAT_AXES + a] = in->rms[a];
        out[2u * FEAT_AXES + a] = in->p2p[a];
    }
    return 0;
}

int feat_classify(const feat_node_t *nodes, uint32_t count, uint32_t depth_max,
                  const int16_t *features, uint32_t nfeat, uint8_t *out)
{
    if (nodes == NULL || features == NULL || out == NULL) {
        return FEAT_ERR_ARGUMENT;
    }
    if (count == 0u || count > FEAT_TREE_MAX) {
        return FEAT_ERR_TREE;
    }
    if (depth_max == 0u) {
        return FEAT_ERR_DEPTH;
    }
    if (nfeat == 0u) {
        return FEAT_ERR_FEATURE;
    }

    uint32_t at = 0u;
    for (uint32_t step = 0u; step <= depth_max; step++) {
        if (at >= count) {
            return FEAT_ERR_NODE;
        }
        const feat_node_t *node = &nodes[at];
        if (node->leaf != 0u) {
            *out = node->feature;
            return 0;
        }
        if ((uint32_t) node->feature >= nfeat) {
            return FEAT_ERR_FEATURE;
        }
        /* At or below the threshold goes left, which is stated here because the
         * boundary case is exactly where two implementations of one tree
         * disagree, and the gate compares classes. */
        at = (features[node->feature] <= node->threshold)
           ? (uint32_t) node->left : (uint32_t) node->right;
    }
    /* The walk did not reach a leaf within the stated depth. On the board that
     * is a missed deadline rather than a wrong answer, so it is a refusal and
     * not a default class. */
    return FEAT_ERR_DEPTH;
}
