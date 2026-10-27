/* winfeat.h: chapter 16's window features and its bounded tree, in Q15.
 *
 * THE NAME IS winfeat AND NOT features, AND THAT IS A COMPILER FACT RATHER THAN
 * A PREFERENCE. glibc ships its own <features.h>, which its <stdint.h> reaches
 * through bits/libc-header-start.h, and a -I directory holding a file of that
 * name shadows it. The build then fails inside glibc with "missing binary
 * operator before token '('", because __GLIBC_USE is never defined and glibc's
 * own #if lines are left with a macro that expanded to nothing. It cannot
 * happen on Windows, which has no glibc features.h, so the authoring laptop
 * could not have seen it; the branch run on the laptop that has a compiler did,
 * on Tuesday 27 October 2026, before main saw any of it.
 *
 * WHAT THIS IS, AND IT IS CRITERION 1. Chapter 16's first acceptance criterion
 * is "the equivalence gate passes: all three implementations produce identical
 * features and identical classes on every window of the held-out set. The build
 * fails if it does not." Step 4 says to build that gate before any leg, because
 * "doing it later never works because by then each implementation has its own
 * quiet assumption". This is the gate and the arithmetic under it. It needs no
 * shield, no sensor and no dataset: identical is identical on any window.
 *
 * ONE WINDOW is 128 samples of three axes, which is step 3's definition, about
 * 1.23 seconds at 104 Hz. Features are Q15 because that is what the most
 * constrained of the three implementations can produce.
 *
 * FINDING ONE: THE DEFINITION NAMES THE QUANTITY AND NOT THE ALGORITHM, AND AN
 * EXACT GATE NEEDS THE ALGORITHM. Step 3 fixes the window, the axes, the feature
 * list and the numeric format. It does not fix the rounding. In Q15 a mean
 * computed as the floor of a sum over 128 and a mean computed as the nearest
 * value differ by one least significant bit on a large share of inputs, and step
 * 4's gate demands equality rather than closeness, so two implementations each
 * obeying the printed definition can fail it. Every rounding in this file is
 * therefore stated: the mean and the root mean square both TRUNCATE TOWARD
 * NEGATIVE INFINITY, and the test demonstrates the disagreement a nearest-value
 * mean would produce rather than describing it.
 *
 * FINDING TWO: THE BAND ENERGIES ARE ABSENT FOR THE SAME REASON, DELIBERATELY.
 * Step 3's feature structure carries four band energies per axis "from the FFT".
 * A quantity named that way cannot enter an exact gate: the transform, its
 * scaling between stages, the window function if any, and the bin ranges that
 * make up each band are all unstated, and in Q15 two legitimate routes to the
 * same band energy differ in the last bits. Chapter 18 is where that transform
 * is specified and measured, and it is bench work. So this file carries the
 * three feature groups whose algorithm can be pinned, the structure says the
 * band group is missing, and the project README says what has to be settled
 * before it can be added. An approximate band energy inside an exact gate would
 * turn criterion 1 from a check into a tolerance nobody wrote down.
 *
 * FINDING THREE: A BOUNDED TREE HAS TO BE BOUNDED BY SOMETHING. Step 5 chooses a
 * tree of bounded depth deliberately, "because bounded depth is bounded latency,
 * and bounded latency is half of what makes a classifier acceptable in a control
 * loop". A walk over nodes that trusts its own child indices is not bounded: a
 * malformed tree with a cycle walks for ever, and on the board that is a missed
 * control deadline rather than a wrong answer. The walk here takes the depth as
 * an argument and refuses when it is exceeded.
 *
 * WHAT IS NOT HERE. The dataset and its protocol, the three legs, the in-sensor
 * program, the neural comparison, and every latency, memory and charge number.
 * Those need the IKS4A1 shield, a Raspberry Pi and the PPK2. There is no
 * measured number in this project, and the features here have been computed only
 * on synthetic windows.
 */
#ifndef WINFEAT_H
#define WINFEAT_H

#include <stdint.h>

#define FEAT_WIN_N 128u
#define FEAT_AXES 3u

/* Three feature groups, one Q15 value per axis each, so nine values per window.
 * The band group of step 3 is absent on purpose; the header says why. */
#define FEAT_GROUPS 3u
#define FEAT_COUNT (FEAT_GROUPS * FEAT_AXES)

typedef struct {
    int16_t mean[FEAT_AXES]; /* truncated toward negative infinity   */
    int16_t rms[FEAT_AXES];  /* integer square root, also truncated  */
    int16_t p2p[FEAT_AXES];  /* max minus min, saturated at Q15 full */
} feat_t;

/* A node is a feature index, a threshold and two children. A leaf points at
 * itself on both sides and carries its class in the feature field, which keeps
 * the structure one size and means a leaf cannot be mistaken for a branch with a
 * missing child. */
#define FEAT_TREE_MAX 64u

typedef struct {
    uint8_t feature;   /* a feature index, or the class when leaf is non-zero */
    uint8_t leaf;      /* non-zero for a leaf                                 */
    int16_t threshold; /* goes left when the feature is at or below this      */
    uint8_t left;
    uint8_t right;
} feat_node_t;

#define FEAT_ERR_ARGUMENT (-1) /* a null pointer, or a window of no samples   */
#define FEAT_ERR_SHAPE    (-2) /* a sample count or axis count not the one in
                                * step 3, refused rather than scaled, because
                                * a feature computed over a different window is
                                * a different feature                         */
#define FEAT_ERR_TREE     (-3) /* an empty tree, or more nodes than the bound */
#define FEAT_ERR_NODE     (-4) /* a child index outside the tree              */
#define FEAT_ERR_FEATURE  (-5) /* a feature index outside the vector          */
#define FEAT_ERR_DEPTH    (-6) /* the walk exceeded the stated depth, which is
                                * what makes the latency bounded              */

/* Features for one window. samples is interleaved by axis, which is the order
 * the sensor's buffer delivers: sample 0 axis 0, sample 0 axis 1, and so on. n
 * and axes must be exactly FEAT_WIN_N and FEAT_AXES. */
int feat_window(const int16_t *samples, uint32_t n, uint32_t axes,
                feat_t *out);

/* The nine features as a flat vector, in the order mean, rms, p2p, each by
 * axis, which is the order a tree's feature indices refer to. */
int feat_vector(const feat_t *in, int16_t *out, uint32_t count);

/* Walk the tree and return a class. depth_max is the bound step 5 relies on. */
int feat_classify(const feat_node_t *nodes, uint32_t count, uint32_t depth_max,
                  const int16_t *features, uint32_t nfeat, uint8_t *out);

#endif /* WINFEAT_H */
