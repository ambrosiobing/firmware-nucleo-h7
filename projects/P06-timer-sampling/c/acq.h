/* acq.h: one interface, three implementations.
 *
 * The point of this header is that main.c is byte identical across all three
 * builds. If the application had to know which acquisition back end it was
 * talking to, the comparison in docs/measurement.md would be comparing three
 * programs rather than three mechanisms.
 *
 * Selected at build time by CMake, which compiles exactly one of
 * acq_systick.c, acq_timer.c or acq_dma_double.c into each target.
 */
#ifndef ACQ_H
#define ACQ_H

#include <stddef.h>
#include <stdint.h>

/* The nominal rate the whole exercise is about. docs/measurement.md states
 * what "exactly" is allowed to mean and what refutes it. */
#define ACQ_RATE_HZ 1000u

/* A block of samples the application may read. In the double-buffered build
 * this is the half the transfer engine is not currently writing; in the other
 * two it is a small buffer the interrupt has finished with. The application
 * does not need to know which. */
typedef struct {
    const uint16_t *samples;  /* the half that is safe to read */
    size_t          count;    /* samples in that half */
    uint32_t        seq;      /* increments once per block, never wraps in a run */
} acq_block_t;

/* Configure and start acquiring. Returns 0 on success, a negative value if a
 * peripheral refused to configure. It does not return an error for anything a
 * self-test can catch later; see acq_selftest. */
int acq_start(void);

/* Take the next completed block. Returns 1 and fills *out when one is ready,
 * 0 when none is. Never blocks. */
int acq_take(acq_block_t *out);

/* Blocks completed but never read by the application. The one number that says
 * whether the application kept up. If this is non-zero the samples are still
 * correct but the record has a gap, and a run with a gap is not evidence. */
uint32_t acq_overruns(void);

/* The CONVERTER'S OWN overrun, which is a different thing from acq_overruns()
 * above, and reporting only the first is what let a run with converter overruns
 * look clean until Thursday 8 October 2026.
 *
 * acq_overruns() counts blocks the APPLICATION never collected. This counts
 * conversions the converter itself could not deliver, because a previous result
 * had not been read out of its data register yet. Both can be zero while the
 * other is not, and they have different causes: the first is main being too
 * slow, the second is the interrupt being too slow or not arriving.
 *
 * RETURNS 0 AND WRITES THE COUNT, OR -1 IF THIS BACK END DOES NOT WATCH THE
 * FLAG AT ALL, and the two answers are deliberately not interchangeable. A
 * count of 0 means the flag was watched throughout and never set. -1 means
 * nobody looked. A back end that cannot report it says so rather than printing
 * a reassuring zero, because a zero that nobody earned is worse than no number:
 * it reads as evidence. */
int acq_conv_overruns(uint32_t *out);

/* Which back end was compiled in, for the banner and for the capture metadata.
 * The string matches the --build argument of witness/scan.py exactly, so the
 * firmware and the capture cannot disagree about what was running. */
const char *acq_name(void);

#endif /* ACQ_H */
