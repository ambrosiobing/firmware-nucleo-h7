/* acq_dma_double.c: build 3, the samples reach memory without the processor.
 *
 * The timer triggers the converter as in build 2, and a transfer engine moves
 * each result into memory. Double buffer mode means the engine holds two
 * memory addresses and swaps between them at the end of each transfer, so one
 * half is always safe for the application to read while the other fills. The
 * processor wakes once per half rather than once per sample.
 *
 * THE TRAP THIS BUILD SITS ON, and it is the most important comment in this
 * repository. On this part the main transfer engines cannot reach the tightly
 * coupled memories at all, and the region they can reach is cached. So:
 *
 *   1. The sample buffer must NOT be placed in tightly coupled memory, or the
 *      transfer silently moves nothing. That is not a coherency problem, it is
 *      a reachability one, and no amount of cache maintenance fixes it.
 *   2. Once it is somewhere reachable, it is cached, so the processor can read
 *      a stale line where the engine has already written. That one is a
 *      coherency problem and does need maintenance or a protection-unit region.
 *
 * Chapter 19 is entirely about these two and separates them properly. This
 * build does the minimum correct thing: the buffer is placed in a named
 * section, and the region is invalidated before the application reads it.
 */
#include "acq.h"
#include "marker.h"

#include "stm32h7xx.h"

#define HALF 64u                       /* samples per half */

/* Placed by name, not left to the linker's default. The section is defined in
 * the linker script and must land in a memory the transfer engine can reach.
 * Aligned to a cache line so an invalidate of this buffer cannot touch
 * anything else, which is the other half of doing maintenance correctly. */
__attribute__((section(".dma_buffer"), aligned(32)))
static uint16_t buffer[2u * HALF];

static volatile uint32_t ready_seq;
static volatile uint32_t taken_seq;
static volatile uint32_t overruns;
static volatile uint8_t  ready_half;    /* the half that just finished */

/* TO BE CONFIRMED against RM0455: which stream of which engine serves this
 * converter, its request multiplexer number, and the interrupt name. All three
 * are table lookups and none is guessed here. The handler name below is a
 * placeholder for whichever stream is chosen. */
void DMA1_Stream0_IRQHandler(void)
{
    /* Which half just completed is told by the engine's current target flag,
     * and the answer is the opposite one: if it is now writing half 1, half 0
     * is the one that finished. */
    uint8_t now_writing = 1u;  /* TO BE CONFIRMED: read the current target bit */
    ready_half = now_writing ^ 1u;

    if (ready_seq != taken_seq) {
        overruns++;
    }
    ready_seq++;

    /* TO BE CONFIRMED: clear the transfer complete flag for this stream. */
}

int acq_start(void)
{
    marker_init();

    ready_seq = 0u;
    taken_seq = 0u;
    overruns = 0u;
    ready_half = 0u;

    for (uint32_t i = 0u; i < 2u * HALF; i++) {
        buffer[i] = 0u;
    }
    /* Clean the buffer out to memory before the engine starts writing it, so
     * no dirty cache line can later be written back over a real sample. This
     * is the direction of the coherency problem that people forget: not only
     * invalidate before reading, but clean before letting the engine write. */
    SCB_CleanDCache_by_Addr((uint32_t *) buffer, (int32_t) sizeof buffer);

    /* TO BE CONFIRMED, and all of it is a table lookup in RM0455 rather than
     * anything to infer: the converter bring-up and its trigger selection as
     * in build 2; the engine, stream and request number; double buffer mode
     * with both memory addresses and the transfer length of HALF; the
     * peripheral address; and the transfer complete interrupt.
     *
     * Deliberately absent rather than plausible. A sequence copied from the
     * better known member of this family is exactly the failure the front
     * matter of the volume warns about. */

    return -3;   /* not configured yet; refusing to run is correct */
}

int acq_take(acq_block_t *out)
{
    uint32_t r = ready_seq;
    if (r == taken_seq) {
        return 0;
    }
    uint8_t h = ready_half;
    uint16_t *start = &buffer[h * HALF];

    /* Invalidate before reading, or the processor may return a line it cached
     * before the engine wrote this half. The length is a whole number of cache
     * lines because the buffer is aligned and HALF is a power of two, which is
     * why both of those were chosen rather than left to chance. */
    SCB_InvalidateDCache_by_Addr((uint32_t *) start, (int32_t) (HALF * sizeof *start));

    out->samples = start;
    out->count   = HALF;
    out->seq     = r;
    taken_seq    = r;
    return 1;
}

uint32_t acq_overruns(void) { return overruns; }

/* NOT WATCHED HERE, so -1, and this is the back end where that answer is least
 * comfortable. A transfer engine reading DR is exactly the arrangement where the
 * converter's OVR is the primary failure mode rather than a remote one: if the
 * engine is not keeping up, or its request is misrouted through DMAMUX1, the
 * symptom is OVR and the application's own block counter would show nothing
 * wrong. So this is the build that most needs the flag and the one least
 * entitled to report it, because it does not configure the converter at all yet.
 * Saying -1 is what keeps that from reading as a clean run later. */
int acq_conv_overruns(uint32_t *out)
{
    (void) out;
    return -1;
}

const char *acq_name(void) { return "dma"; }
