/* mpualign.h: chapter 19's alignment arithmetic, which is criteria 4 and 5.
 *
 * WHAT THIS IS, AND WHY IT NEEDS NO TRANSFER ENGINE. Chapter 19's fourth
 * acceptance criterion is "every buffer in the repository is aligned to 32
 * bytes and sized to a multiple of 32 bytes, and both facts are enforced by a
 * build-time assertion rather than by a comment", and its fifth is "the region
 * table is applied at startup and a deliberately misaligned entry stops the
 * build, not the board". Both are arithmetic. The stale read itself needs a
 * transfer engine and RM0455; deciding WHICH buffers can produce one does not.
 *
 * THIS GIVES P19 ITS FIRST TWIN. Until Wednesday 28 October 2026 this project
 * held one implementation in one language, the instruction cache enable in
 * c/board/icache.c, and therefore had no equivalence gate at all. Every other
 * project in the volume has at least two implementations to disagree, and that
 * comparison found a real defect in P16 and another in P20 on two consecutive
 * days. This file is the half of P19 that can be compared.
 *
 * THE CACHE LINE IS 32 BYTES ON THIS PART AND THAT FIGURE IS MEASURED HERE, not
 * quoted: P19's own placement experiment on Saturday 3 October 2026 found the
 * cold cost tracking the offset within a 32 byte granule, 3220.99 cycles at
 * offset 0 against 3163.98 at offset 16, and flat at 2378.99 across all four
 * offsets once cached. The line length is still a parameter of every function
 * here, because a figure measured on one part is not a constant of the
 * arithmetic.
 *
 * FINDING ONE: A CLEAN MAY OVER-EXTEND AND AN INVALIDATE MAY NOT, AND THE
 * CHAPTER TREATS THEM AS ONE OPERATION. Both are maintenance by address range,
 * both round the range outward to whole lines, and the rounding is where they
 * stop being the same thing. A line that holds the buffer's bytes AND a
 * neighbour's:
 *
 *   cleaning it      writes the neighbour's bytes back to memory, which is
 *                    what they already say, so the effect is nothing
 *   invalidating it  DISCARDS the neighbour's bytes, losing a write the
 *                    processor has made and has not yet written back
 *
 * So the identical range arithmetic is harmless for one operation and a silent
 * corruption of unrelated data for the other. That asymmetry is the whole
 * reason criterion 4 is a build-time assertion rather than a recommendation,
 * and the chapter states the rule without stating the reason. This library
 * reports the two shared lines separately so a caller can be refused for the
 * operation that cannot tolerate them.
 *
 * FINDING TWO: CRITERION 4'S TWO HALVES FIX DIFFERENT ENDS AND NEITHER IS
 * ENOUGH. Aligning the start to a line removes the shared head line. Sizing to
 * a whole number of lines removes the shared tail. A buffer that is aligned but
 * not a multiple shares its tail; one that is a multiple but not aligned shares
 * both. The criterion asks for both facts and a reader can satisfy one and
 * believe it is done, so the oracle carries a case for each.
 *
 * FINDING THREE: THE MPU'S ALIGNMENT IS TO THE REGION'S OWN SIZE, NOT TO THE
 * CACHE LINE, SO CRITERION 4 AND CRITERION 5 ARE INDEPENDENT. A region of 4096
 * bytes at 0x24000100 satisfies criterion 4 exactly, being 32 byte aligned, and
 * fails criterion 5, because an ARMv7-M region must start on a multiple of its
 * own size. Passing one check says nothing about the other, and this is the
 * mistake a reader makes who has internalised "align to 32 and you are safe".
 *
 * FINDING FOUR: THE END ADDRESS CAN WRAP, AND A WRAPPED RANGE DOES NOTHING
 * QUIETLY. A buffer near the top of a 32-bit address space has an end address
 * that wraps to a small number, and the aligned end then compares BELOW the
 * aligned base. A maintenance loop written as "from base to end" then executes
 * zero iterations: no fault, no diagnostic, and the cache is not maintained at
 * all, which is the failure this chapter exists to describe arriving by
 * arithmetic rather than by a missing call. Refused here.
 *
 * AND THE BOUNDARY OF THAT FINDING IS WHERE THE REPRESENTATION RUNS OUT, which
 * is worth separating from the wrap itself. `end` is one past the last
 * address, so a range ending exactly at the top of the address space needs the
 * value 2 to the 32nd and a uint32_t has only zero to offer. Storing zero is
 * not a near miss, it IS the failure above: a loop from base to end runs zero
 * times. So that case is refused rather than encoded, and the refusal is the
 * honest form of a signature that cannot express the answer. This is the same
 * rule that cost two commits on P18 on Monday 26 October 2026, where an oracle
 * case the C's signature could not answer was exempted instead of the
 * signature being changed.
 *
 * WHAT IS NOT HERE. The transfer engine, the stale read reproduced across a
 * thousand runs, the three fixes and their failure rates, the two cycle costs
 * per transfer, the reachability table against RM0455, and the fourth failure
 * with its register evidence. Those need the board, an ADXL345 on a breadboard
 * and RM0455. There is no measured number in this file; the one measured
 * figure it quotes, the 32 byte line, came from this project's own run on the
 * board on Saturday 3 October 2026 and is named rather than assumed.
 */
#ifndef MPUALIGN_H
#define MPUALIGN_H

#include <stdint.h>

/* This part's cache line, measured by P19's placement experiment on Saturday
 * 3 October 2026. Every function still takes the line as an argument. */
#define MPU_LINE 32u

/* ARMv7-M's smallest region, and the largest this signature can express. The
 * architecture also allows a region covering the whole address space, which is
 * 2 to the 32nd and does NOT fit in the uint32_t this function takes. That is
 * named here rather than silently excluded: a caller who needs it needs a
 * different signature, and the refusal below says size and not align. */
#define MPU_MIN_REGION 32u
#define MPU_MAX_REGION 0x80000000u

typedef struct {
    uint32_t base;      /* the aligned start of the maintenance range      */
    uint32_t end;       /* one past the aligned end                        */
    uint32_t bytes;     /* end - base                                      */
    uint32_t lines;     /* bytes / line                                    */
    uint8_t head_shared; /* the first line also holds bytes before the
                          * buffer, so invalidating it discards them       */
    uint8_t tail_shared; /* the last line also holds bytes after it         */
} mpu_range_t;

#define MPU_ERR_ARGUMENT (-1) /* a null pointer                            */
#define MPU_ERR_LINE     (-2) /* a line length of zero, or one that is not
                               * a power of two, which would make the
                               * rounding below meaningless                */
#define MPU_ERR_LENGTH   (-3) /* a buffer of no bytes, which has no range
                               * to maintain and is a caller's mistake
                               * rather than an empty answer               */
#define MPU_ERR_WRAP     (-4) /* the end address wraps past 32 bits, which
                               * is finding four                           */
#define MPU_ERR_SIZE     (-5) /* a region size of zero, not a power of two,
                               * below MPU_MIN_REGION, or above what this
                               * signature can express                     */
#define MPU_ERR_ALIGN    (-6) /* a region base that is not a multiple of
                               * its own size, which is finding three      */

/* The whole lines a maintenance operation over [addr, addr + len) touches,
 * with the two shared lines reported separately. */
int mpu_maint_range(uint32_t addr, uint32_t len, uint32_t line,
                    mpu_range_t *out);

/* Criterion 4, as one answer: aligned to a line AND a whole number of lines.
 * Returns 0 when both hold, and the range's two flags say which failed. */
int mpu_buffer_check(uint32_t addr, uint32_t len, uint32_t line);

/* Criterion 5: an ARMv7-M region's SIZE field, which is log2(size) - 1, and
 * the base checked against the region's own size. */
int mpu_region_encode(uint32_t base, uint32_t size, uint8_t *size_field);

#endif /* MPUALIGN_H */
