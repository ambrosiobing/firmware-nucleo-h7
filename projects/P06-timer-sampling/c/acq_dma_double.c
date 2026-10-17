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
#include "acq_errors.h"
#include "marker.h"

#include "stm32h7xx.h"

#define HALF 64u                       /* samples per half */

/* Placed by name, not left to the linker's default. The section is defined in
 * the linker script and must land in a memory the transfer engine can reach.
 * Aligned to a cache line so an invalidate of this buffer cannot touch
 * anything else, which is the other half of doing maintenance correctly.
 *
 * AND THE NAME WAS WRONG BY ONE LETTER UNTIL Friday 16 October 2026, which is
 * the whole of the trap above happening to this file. It read ".dma_buffer";
 * the linker script collects ".dma_buffers" and ".dma_buffers*", and an ld
 * pattern ending in a star matches names that START with it, so a name one
 * character shorter matched neither and NO RULE PLACED THIS BUFFER. An orphan
 * section goes where the linker likes, which on this part is how a buffer ends
 * up in a memory the transfer engines cannot reach, moving nothing and
 * reporting no error. Found by reading the file rather than by building it, in
 * code that has never run, and python/tools/check_section_placement.py now
 * compares every section attribute against the script's rules on every push. */
__attribute__((section(".dma_buffers"), aligned(32)))
static uint16_t buffer[2u * HALF];

static volatile uint32_t ready_seq;
static volatile uint32_t taken_seq;
static volatile uint32_t overruns;
static volatile uint8_t  ready_half;    /* the half that just finished */

/* THE ENGINE, THE STREAM, THE REQUEST AND THE INTERRUPT ARE SETTLED, Friday
 * 16 October 2026, and the source is ST's own example for THIS BOARD rather than
 * for the family: Projects/NUCLEO-H7A3ZI-Q/Examples/ADC/ADC_DMA_Transfer in
 * STM32CubeH7, whose stm32h7xx_hal_msp.c configures
 *
 *   DmaHandle.Instance = DMA1_Stream1;
 *   DmaHandle.Init.Request = DMA_REQUEST_ADC1;
 *   DmaHandle.Init.Direction = DMA_PERIPH_TO_MEMORY;
 *   DmaHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
 *   DmaHandle.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
 *   HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
 *
 * and DMA_REQUEST_ADC1 is 9 in stm32h7xx_hal_dma.h, defined unconditionally for
 * the whole H7 family as "DMAMUX1 ADC1 request". Two sources, and they agree.
 *
 * ONE OF THE FOUR IS A CHOICE AND NOT A LOOKUP, which is worth separating or the
 * next reader will treat ST's example as a constraint. On a part with a request
 * multiplexer ANY stream can serve ANY request: the multiplexer channel belonging
 * to the stream is written with the request number, so stream 1 is ST's choice
 * and stream 0 would be equally correct. What is NOT free is the request number,
 * which is the converter's identity on DMAMUX1, and the engine: DMA1 and DMA2 are
 * the engines that take DMAMUX1 requests and reach AXI SRAM, while BDMA sits in
 * the other domain behind DMAMUX2. Stream 1 is kept here only to match the
 * example, so that a reader comparing the two files is not asked to track a
 * difference that means nothing.
 *
 * WHAT IS STILL TO BE CONFIRMED, and it is narrower than it was. The example uses
 * circular mode with one buffer; this build wants double buffer mode, which is
 * HAL_DMAEx_MultiBufferStart_IT in stm32h7xx_hal_dma_ex.h, taking a second memory
 * address. The registers behind it, the double buffer bit and the current target
 * bit in the stream's control register, the second memory address register, and
 * the transfer complete flag and its clear register for stream 1, are RM0455
 * tables and the device header's bit definitions. They are not written here
 * until they are read, and the handler below is therefore still a shell.
 *
 * AND THE VECTOR EXISTS NOW, WHICH IT DID NOT WHEN THIS HANDLER WAS WRITTEN.
 * Renaming it from stream 0 to stream 1 on Friday 16 October 2026 prompted a
 * one-line check of c/board/startup.c, which had no DMA entry at all: this
 * handler compiled, linked and could never have run, whatever the engine
 * configuration said. That is the three-things-in-a-row failure recorded at
 * length in startup.c and in stm32h7a3_regs.h, which cost P06 an evening on
 * Wednesday 7 October 2026 through ADC_IRQHandler. The slot is filled from ST's
 * IRQn_Type, position 12, and python/tools/check_handler_vectors.py now refuses
 * any defined handler this repository has no vector for. */
void DMA1_Stream1_IRQHandler(void)
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

    /* THE WHOLE SEQUENCE IS NOW SOURCED, Saturday 17 October 2026, AND NONE OF IT
     * IS WRITTEN YET. Reading came first on purpose; the code follows in one
     * piece once the decision at the end of this block is made. Every line below
     * carries where it came from, and all of it is ST's own material for this
     * part or for this board rather than for the family.
     *
     * THE CONVERTER'S DATA MANAGEMENT, AND THIS ONE CHANGES THE DESIGN. On this
     * part the converter does not raise a transfer request unless CFGR's DMNGT
     * field says so: 00 leaves data in DR alone, 01 is one shot, 11 is circular,
     * from ADC_CONVERSIONDATA_* in stm32h7xx_hal_adc.h, which defines them as
     * ADC_CFGR_DMNGT_0 and _1. THE SHARED BRING-UP LEAVES DMNGT AT 00, which is
     * right for the other two back ends and wrong for this one, so this file owes
     * one converter write of its own, after adc_bring_up() returns and before the
     * timer starts, reported in the same wrote-and-read-back form as the eight
     * steps so it cannot pass unnoticed. Without it everything else here would be
     * configured correctly and nothing would ever move, which is this project's
     * characteristic failure and is worth one line of comment per occurrence.
     *
     * AND IT IS NOT THE ONLY CONVERTER FIELD THIS FILE OWES A DECISION ON.
     * OVRMOD is left at its reset value too, reported as found by acq_timer.c
     * and chosen by nobody, and it decides what an overrun does to the data:
     * clear preserves the old sample, set lets the new one overwrite it. For the
     * two back ends whose HANDLER reads DR that was a reading nothing depended
     * on. For this one, whose ENGINE reads DR, the question is different and the
     * answer is not inherited: this file counts overruns of its own, and what a
     * converter overrun does to a double-buffered stream has to be decided
     * before the count means anything. Recorded here as the second open field
     * rather than written, for the same reason as the first.
     *
     * THE REQUEST ROUTING. The multiplexer channel belonging to a stream is
     * DMAMUX1_Channel0 plus the stream number, from HAL_DMA_Init in
     * stm32h7xx_hal_dma.c, which computes
     *     DMAmuxChannel = DMAMUX1_Channel0 + stream_number * 4
     * over an array of channels, and writes the request with
     *     DMAmuxChannel->CCR = Request & DMAMUX_CxCR_DMAREQ_ID
     * So stream 1 is routed by DMAMUX1_Channel1 and the value is 9, which is
     * DMA_REQUEST_ADC1 from stm32h7xx_hal_dma.h.
     *
     * DOUBLE BUFFER MODE, from HAL_DMAEx_MultiBufferStart_IT and the helper it
     * calls in stm32h7xx_hal_dma_ex.c, in the order that function writes them:
     *     CR  |= DMA_SxCR_DBM
     *     M1AR = the second memory address
     *     NDTR = the transfer length
     *     PAR  = the peripheral address, for peripheral to memory
     *     M0AR = the first memory address, for peripheral to memory
     *     CR  |= the transfer complete, transfer error and direct mode error
     *            interrupt enables
     *     CR  |= DMA_SxCR_EN, last
     * The length is HALF, the peripheral address is &ADC1->DR, and the two memory
     * addresses are the two halves of this file's buffer. The stream must be
     * disabled and its enable bit seen to clear before any of this, which is the
     * one ordering rule the HAL enforces separately.
     *
     * WHICH HALF FINISHED, which the handler below needs and currently guesses.
     * The current target bit is DMA_SxCR_CT in the stream's control register,
     * from LL_DMA_GetCurrentTargetMem in stm32h7xx_ll_dma.h, and the engine is
     * writing the half that bit names, so the finished half is the other one.
     *
     * THE FLAG AND ITS CLEAR, for stream 1 specifically. The device header names
     * them per stream, so no bit arithmetic is needed and none is done here:
     * DMA_LISR_TCIF1 to test, from LL_DMA_IsActiveFlag_TC1 in
     * stm32h7xx_ll_dma.h, and DMA_LIFCR_CTCIF1 to clear by the same naming. The
     * clear macro's name is the only thing in this block not quoted from a file
     * I have read, and a wrong macro name is a compile error rather than a silent
     * failure, which is the safe direction for the one gap.
     *
     * AND ONE DECISION IS NOT MINE. This back end needs TIM6 running at the
     * sampling rate exactly as the timer back end does, and that setup lives
     * inline in acq_timer.c. Three ways, as with the prescaler reader on Friday
     * 9 October 2026:
     *   A. duplicate the TIM6 setup here, with the duplication named in a comment
     *      rather than hidden, which is what option A did for apb1_divisor.
     *   B. factor it into a shared tim6_bring_up() beside adc_bringup.c, which is
     *      the same move that file was and would be checked the same way, by
     *      counting console-affecting strings before and after.
     *   C. leave this back end refusing until the factoring is done as part of a
     *      wider decision about what else shares a timer.
     * B is the one I would choose, because the converter bring-up's factoring
     * paid for itself within two days and the check that made it safe already
     * exists. It is recorded as a question rather than taken, because the last
     * one of these was Joseph's and the reasoning that applied then applies now.
     *
     * Deliberately absent rather than plausible. A sequence copied from the
     * better known member of this family is exactly the failure the front
     * matter of the volume warns about, and nothing above was taken from that
     * family's documentation where a file for this part existed instead. */

    /* Refusing to run is correct, and the CODE changed on Friday 9 October 2026
     * while the behaviour did not. This returned -3, which is
     * ACQ_ERR_APB1_PRESCALER in the timer back end behind the same interface, so
     * main.c printed a number whose documented meaning was a clock prescaler that
     * had never been the problem. See acq_errors.h. */
    return ACQ_ERR_DMA_UNCONFIGURED;
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
