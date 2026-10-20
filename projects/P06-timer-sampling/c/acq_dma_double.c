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
#include "adc_bringup.h"
#include "marker.h"
#include "tim6_bringup.h"
#include "board.h"

#include "stm32h7xx.h"

#define HALF 64u                       /* samples per half */

/* The stream this back end uses, and the request number that reaches it.
 *
 * STREAM 1 IS ST'S CHOICE AND NOT A CONSTRAINT, which the comment above the
 * handler argues at length: on a part with a request multiplexer any stream can
 * serve any request. It is kept to match ST's example for this board so that a
 * reader comparing the two files tracks no difference that means nothing.
 *
 * The request value 9 is NOT free: it is the converter's identity on DMAMUX1,
 * DMA_REQUEST_ADC1 in stm32h7xx_hal_dma.h. It is spelled out here rather than
 * included from the HAL, because this project links no HAL at all.
 *
 * The poll bound is orders of magnitude of headroom rather than a tuned number:
 * a stream that has been told to stop clears its enable bit in a few cycles, so
 * anything that reaches this count is a stream that is not responding. */
#define DMA_STREAM_NUMBER      1u
#define DMA_REQUEST_ADC1_ID    9u
#define DMA_STOP_POLLS_MAX     1000000u

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

/* The converter's own overrun count, separate from `overruns` above and
 * answering a different question: this one is the converter unable to deliver a
 * conversion because the previous result had not left DR, which on this back end
 * means the engine did not serve the request. Watched since Tuesday
 * 20 October 2026, so acq_conv_overruns reports a real count rather than -1. */
static volatile uint32_t conv_overruns;
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
 * ALL OF IT IS NOW READ AND WRITTEN, Monday 19 October 2026, and this paragraph
 * said the opposite until that day. It listed the double buffer bit, the current
 * target bit, the second memory address register and the transfer complete flag
 * with its clear register as unread, and called the handler below a shell. Every
 * one of those was read from stm32h7a3xxq.h, the Q-suffix header for this exact
 * part, with the line numbers quoted beside each use in acq_start, and RM0455 was
 * not needed for any of them.
 *
 * THE ONE QUESTION THE HEADER READING COULD NOT ANSWER IS ANSWERED, from
 * RM0455 Rev 13 on Tuesday 20 October 2026, and the answer is that not writing
 * CIRC is correct for a reason stronger than the sourcing. Section 15.3.11,
 * Double-buffer mode, page 605: when double buffer mode is enabled the circular
 * mode is automatically enabled, the CIRC bit is not relevant, and the memory
 * pointers are swapped at each end of transaction. The bit's own description,
 * page 622, says the same thing from the other side: CIRC is forced to 1 by
 * hardware if DBM is set, as soon as the stream is enabled.
 *
 * SO ONE ENTRY COMES OFF THE FIRST RUN'S SUSPECT LIST. This file said that if
 * the stream delivered two halves and stopped, CIRC was the first thing to read.
 * It cannot be the cause: the hardware sets it when EN goes to 1. A stream that
 * stops after two halves has some other reason, and the first thing to read is
 * then the converter's OVR flag, for the reason given at acq_conv_overruns.
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
/* THE CONVERTER'S HANDLER, WHICH THIS BACK END DID NOT HAVE UNTIL Tuesday
 * 20 October 2026. It handles exactly one flag. There is no end-of-conversion
 * work here and no EOCIE: nothing in this build reads DR, which is the whole
 * point of it, so the only converter event worth an interrupt is the one that
 * stops the stream.
 *
 * COUNTING WITHOUT CLEARING WOULD BE WORSE THAN NOT WATCHING, which acq_timer.c
 * says of its own handler: the flag stays set, the interrupt re-enters forever,
 * and the count means nothing. Clearing it also unblocks the engine's requests,
 * per RM0455 page 980.
 *
 * WHAT THE MANUAL DID NOT SAY, left open rather than guessed and now reachable:
 * clearing OVR unblocks the requests, and nothing read so far says what then
 * holds between the stream's current target, its remaining NDTR and the samples
 * already placed. A run that recovers from an overrun with its halves still
 * aligned and one that recovers shifted look the same from this counter. The
 * first run is what can answer it, and this handler is what makes the question
 * observable at all. */
void ADC_IRQHandler(void)
{
    if ((ADC1->ISR & ADC_ISR_OVR) != 0u) {
        conv_overruns++;
        ADC1->ISR = ADC_ISR_OVR;       /* write one to clear, not a read-modify */
    }
}

void DMA1_Stream1_IRQHandler(void)
{
    /* Which half just completed is told by the engine's current target flag,
     * and the answer is the opposite one: if it is now writing half 1, half 0
     * is the one that finished. */
    const uint8_t now_writing =
        ((DMA1_Stream1->CR & DMA_SxCR_CT) != 0u) ? 1u : 0u;
    ready_half = now_writing ^ 1u;

    if (ready_seq != taken_seq) {
        overruns++;
    }
    ready_seq++;

    /* Clear by writing one to the stream's own bit in the low interrupt flag
     * clear register. Named per stream by the device header, so no arithmetic:
     * DMA_LIFCR_CTCIF1 is bit 11, read Monday 19 October 2026 from
     * stm32h7a3xxq.h line 6889, which is the one name Saturday 17 October
     * 2026's specification could not quote from a file it had read. */
    DMA1->LIFCR = DMA_LIFCR_CTCIF1;
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

    /* WRITTEN ON MONDAY 19 OCTOBER 2026, AND EVERY NAME IN IT WAS READ FIRST.
     * The specification this replaces was carried here unwritten from Saturday
     * 17 October 2026 on purpose, and the four names it could not quote were read
     * on Monday 19 October 2026 from stm32h7a3xxq.h in STM32Cube_FW_H7_V1.13.0,
     * the Q-suffix header for this exact part, at the line numbers given below.
     *
     * NONE OF THIS HAS BEEN COMPILED. The three P06 targets are not built by
     * continuous integration, which says so in firmware.yml's own header because
     * they include the vendor device header this repository does not vendor, and
     * the one laptop that could build them had its toolchain removed. So this is
     * code written from sourced names and checked by nothing yet, which is a
     * weaker claim than anything else in this file, and the state table says so.
     *
     * THE ORDER IS ST'S, NOT MINE. Everything below follows
     * HAL_DMAEx_MultiBufferStart_IT and the helper it calls in
     * stm32h7xx_hal_dma_ex.c: double buffer bit, second address, length,
     * peripheral address, first address, interrupt enables, stream enable last.
     * Where this file departs from that order it would be a defect, so the order
     * is worth checking against that function rather than against taste. */

    /* The converter first, because its trigger is what the timer exists to make
     * and because the field this file owes it can only be written while no
     * conversion is running. */
    const int tim_rc = tim6_bring_up(ACQ_RATE_HZ);
    if (tim_rc != 0) {
        return tim_rc;
    }

    const uint32_t pclk1 = board_pclk1_hz();

    const int adc_rc = adc_bring_up(pclk1, apb1_divisor(),
                                    ADC_EXTSEL_TIM6_TRGO, ADC_EXTEN_RISING,
                                    "TIM6 TRGO on the rising edge");
    if (adc_rc != 0) {
        return adc_rc;
    }

    /* THE ONE CONVERTER FIELD THIS BACK END OWES, and the reading that changed
     * this file's design. On this part the converter raises no transfer request
     * at all unless CFGR's DMNGT field says so: 00 leaves the sample in DR, 11 is
     * circular, from ADC_CONVERSIONDATA_* in stm32h7xx_hal_adc.h, which defines
     * them over ADC_CFGR_DMNGT_0 and _1. The shared bring-up leaves it at 00,
     * which is right for the two back ends whose handler reads DR and wrong for
     * this one, and without this write every other register here would read back
     * correctly and nothing would ever move.
     *
     * Written HERE and not in the shared file, because the field belongs to
     * whichever back end moves the samples. Written AFTER adc_bring_up returns
     * and BEFORE the converter is armed, which is the only window: these bits are
     * write protected while a conversion is running, and ADSTART is still clear.
     * Reported in the same wrote-and-read-back form as the eight shared steps so
     * it cannot pass unnoticed. */
    const uint32_t dmngt_circular = ADC_CFGR_DMNGT_0 | ADC_CFGR_DMNGT_1;
    ADC1->CFGR = (ADC1->CFGR & ~ADC_CFGR_DMNGT) | dmngt_circular;
    const bool dmngt_ok = (ADC1->CFGR & ADC_CFGR_DMNGT) == dmngt_circular;
    adc_report("data management circular", dmngt_circular, ADC1->CFGR, dmngt_ok);
    if (!dmngt_ok) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    /* THE OVERRUN FLAG IS WATCHED, WHICH IS A DECISION TAKEN ON Tuesday
     * 20 October 2026 and not a field this file found set. The reasoning is
     * Joseph's and is better than the choice: an overrun that reports itself is
     * a measured defect, and one that shows up as a console going quiet is an
     * unmeasured one. The timer back end has had this pattern since Saturday
     * 17 October 2026 and it is proven on the board, so adopting it whole makes
     * the two back ends report the same failure the same way.
     *
     * IT MATTERS MORE HERE THAN THERE, for the reason RM0455 page 980 gives: an
     * overrun on this converter does not lose one sample, it blocks every
     * further transfer request until software clears the flag. Unwatched, the
     * first overrun would end the stream, the transfer complete interrupt would
     * stop arriving, the block counter would stop advancing, and no line in the
     * output would say why.
     *
     * STALE FLAGS FIRST, then the enable, then the read-back. Clearing OVR
     * before enabling the interrupt is what keeps a flag set by the bring-up's
     * own conversions from counting as a run's first overrun. */
    ADC1->ISR = ADC_ISR_OVR;
    ADC1->IER |= ADC_IER_OVRIE;
    const bool ovrie = (ADC1->IER & ADC_IER_OVRIE) != 0u;
    adc_report("overrun irq", ADC_IER_OVRIE, ADC1->IER, ovrie);
    if (!ovrie) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    /* The converter's vector is the one ADC_IRQHandler below reaches through,
     * and it exists: startup.c declares it weak at IRQ_ADC and
     * check_handler_vectors.py refuses any defined handler with no vector. */
    NVIC_SetPriority(ADC_IRQn, 5u);
    NVIC_EnableIRQ(ADC_IRQn);

    /* The engine's clock. RCC_AHB1ENR_DMA1EN is bit 0, read Monday 19 October
     * 2026 from stm32h7a3xxq.h line 13546, the sibling of the ADC12EN the shared
     * bring-up already writes in the same register.
     *
     * NOTHING CLOCKS THE REQUEST MULTIPLEXER SEPARATELY, and that is a read
     * result rather than an assumption: this header defines no
     * RCC_AHB1ENR_DMAMUX name at all, and HAL_DMA_Init writes no such bit. */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void) RCC->AHB1ENR;               /* the write is posted; read it back */
    const bool dma_clocked = (RCC->AHB1ENR & RCC_AHB1ENR_DMA1EN) != 0u;
    adc_report("dma1 clock", RCC_AHB1ENR_DMA1EN, RCC->AHB1ENR, dma_clocked);
    if (!dma_clocked) {
        return ACQ_ERR_DMA_UNCONFIGURED;
    }

    /* The stream must be disabled and SEEN to clear before anything else is
     * written to it, which is the one ordering rule the HAL enforces separately
     * from the register order below. EN reads low when the stream is ready,
     * which the device header says in its own comment at line 6690. The wait is
     * bounded because nothing in this volume waits forever. */
    DMA1_Stream1->CR &= ~DMA_SxCR_EN;
    uint32_t polls = 0u;
    while (((DMA1_Stream1->CR & DMA_SxCR_EN) != 0u) && (polls < DMA_STOP_POLLS_MAX)) {
        polls++;
    }
    const bool stream_idle = (DMA1_Stream1->CR & DMA_SxCR_EN) == 0u;
    adc_report("dma stream idle", polls, DMA1_Stream1->CR, stream_idle);
    if (!stream_idle) {
        return ACQ_ERR_DMA_UNCONFIGURED;
    }

    /* THE REQUEST ROUTING. The multiplexer channel belonging to a stream is
     * DMAMUX1_Channel0 advanced by the stream number, from HAL_DMA_Init's own
     * arithmetic in stm32h7xx_hal_dma.c, which computes the channel as
     * DMAMUX1_Channel0 plus stream_number times the size of one channel and then
     * writes CCR = Request & DMAMUX_CxCR_DMAREQ_ID. That field is eight bits at
     * bit 0, read Monday 19 October 2026 from line 7003. The request value is 9,
     * which is DMA_REQUEST_ADC1 in stm32h7xx_hal_dma.h, defined for the whole
     * family as the DMAMUX1 ADC1 request.
     *
     * The arithmetic is kept rather than a named channel macro, because the
     * arithmetic is what was read.
     *
     * AND THIS IS THE ONE CONSTRUCT HERE WHOSE DECLARATION I HAVE NOT READ.
     * Indexing DMAMUX1_Channel0 assumes the device header defines it as a
     * pointer to one channel's registers, which is what HAL's own byte
     * arithmetic over it implies and what every other CMSIS peripheral macro
     * does, but the definition itself was not opened. A wrong assumption there
     * is a COMPILE ERROR rather than a silent misconfiguration, which is the
     * same safe direction Saturday 17 October 2026 accepted for the flag's clear
     * macro, and that one turned out to be right. */
    DMAMUX1_Channel0[DMA_STREAM_NUMBER].CCR =
        DMA_REQUEST_ADC1_ID & DMAMUX_CxCR_DMAREQ_ID;
    const uint32_t ccr = DMAMUX1_Channel0[DMA_STREAM_NUMBER].CCR;
    const bool routed = (ccr & DMAMUX_CxCR_DMAREQ_ID) == DMA_REQUEST_ADC1_ID;
    adc_report("dmamux request 9", DMA_REQUEST_ADC1_ID, ccr, routed);
    if (!routed) {
        return ACQ_ERR_DMA_UNCONFIGURED;
    }

    /* THE STREAM ITSELF, in ST's order. The control word is assembled whole
     * rather than read-modified, because the stream is idle and every field in it
     * is this file's to choose:
     *
     *   DBM    bit 18, double buffer mode, line 6640
     *   DIR    bits 7:6, left at 00 for peripheral to memory, line 6670. Written
     *          as a cleared field rather than omitted, so the intent is in the
     *          code and not only in the silicon's reset value.
     *   MINC   bit 10, memory increment, line 6661. The peripheral address does
     *          not increment, which is the default and is correct: DR is one
     *          register.
     *   PSIZE  bits 12:11 = 01, halfword, line 6657
     *   MSIZE  bits 14:13 = 01, halfword, line 6652
     *
     * Halfword both sides because the converter is configured for 16 bits and the
     * buffer is uint16_t, and because ST's example for this board sets both to
     * halfword.
     *
     * CIRC, bit 8, is NOT written, and as of Tuesday 20 October 2026 that is
     * correct by the manual rather than only by the sourcing. RM0455 Rev 13
     * page 622: CIRC is forced to 1 by hardware if DBM is set, as soon as the
     * stream is enabled. Page 605 says it from the other side, that double
     * buffer mode enables circular mode automatically and the bit is not
     * relevant. Writing it would change nothing; leaving it alone is not a
     * gamble that happened to pay. */
    DMA1_Stream1->CR = DMA_SxCR_DBM
                     | DMA_SxCR_MINC
                     | DMA_SxCR_PSIZE_0
                     | DMA_SxCR_MSIZE_0;

    DMA1_Stream1->M1AR = (uint32_t) &buffer[HALF];
    DMA1_Stream1->NDTR = HALF;
    DMA1_Stream1->PAR  = (uint32_t) &ADC1->DR;
    DMA1_Stream1->M0AR = (uint32_t) &buffer[0];

    /* The three interrupt enables ST's helper sets: transfer complete, transfer
     * error and direct mode error, at bits 4, 2 and 1, lines 6678, 6684 and 6687.
     * The two error enables are set even though this file's handler ignores them,
     * because an unhandled error flag that nobody enabled is indistinguishable
     * from a stream that simply stopped. */
    DMA1_Stream1->CR |= DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;

    const uint32_t cr_wanted = DMA_SxCR_DBM | DMA_SxCR_MINC
                             | DMA_SxCR_PSIZE_0 | DMA_SxCR_MSIZE_0
                             | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    const bool cr_ok = (DMA1_Stream1->CR & ~DMA_SxCR_EN) == cr_wanted;
    adc_report("dma stream configured", cr_wanted, DMA1_Stream1->CR, cr_ok);
    if (!cr_ok) {
        return ACQ_ERR_DMA_UNCONFIGURED;
    }

    const bool addrs_ok = (DMA1_Stream1->M0AR == (uint32_t) &buffer[0])
                       && (DMA1_Stream1->M1AR == (uint32_t) &buffer[HALF])
                       && (DMA1_Stream1->NDTR == HALF);
    adc_report("dma both halves", HALF, DMA1_Stream1->NDTR, addrs_ok);
    if (!addrs_ok) {
        return ACQ_ERR_DMA_UNCONFIGURED;
    }

    /* The vector exists and the handler is reachable; what remains is to let the
     * controller deliver the line. The priority matches the converter's in the
     * other two back ends, so no back end's handler can pre-empt another's. */
    NVIC_SetPriority(DMA1_Stream1_IRQn, 5u);
    NVIC_EnableIRQ(DMA1_Stream1_IRQn);

    /* Stale flags cleared before the stream runs, for the reason acq_timer.c
     * gives about the converter's: a left-over transfer complete would make the
     * first interrupt report a half that predates this call. */
    DMA1->LIFCR = DMA_LIFCR_CTCIF1;

    /* The stream last of the engine's writes, which is ST's order and also the
     * only safe one: every address and length it will use is already in place. */
    DMA1_Stream1->CR |= DMA_SxCR_EN;
    const bool running = (DMA1_Stream1->CR & DMA_SxCR_EN) != 0u;
    adc_report("dma stream enabled", DMA_SxCR_EN, DMA1_Stream1->CR, running);
    if (!running) {
        return ACQ_ERR_DMA_UNCONFIGURED;
    }

    /* Arm the converter BEFORE the trigger source starts, so the first update
     * event is not emitted into an unarmed converter and lost. ADSTART is
     * read-set, so it is written with the same mask discipline as the rest. */
    ADC1->CR = (ADC1->CR & ~ADC_CR_READ_SET_BITS) | ADC_CR_ADSTART;
    const bool armed = (ADC1->CR & ADC_CR_ADSTART) != 0u;
    adc_report("armed for the trigger", ADC_CR_ADSTART, ADC1->CR, armed);
    if (!armed) {
        return ACQ_ERR_ADC_CHANNEL;
    }

    tim6_start();
    return 0;
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

/* WATCHED SINCE Tuesday 20 October 2026, so this returns a real count, and the
 * four paragraphs this comment used to carry are compressed into one history
 * below rather than deleted. A transfer engine is exactly the arrangement where
 * the converter's OVR is the primary failure mode rather than a remote one: if
 * the engine is not keeping up, or its request is misrouted through DMAMUX1, the
 * symptom is OVR and the application's own block counter would show nothing
 * wrong. This was the build that most needed the flag and, until that day, the
 * one least entitled to report it.
 *
 * HOW THE ANSWER GOT HERE, in four steps, because the reasoning is the useful
 * part and each step was wrong in a different way.
 *
 * It first said the back end does not configure the converter at all. That
 * stopped being true on Monday 19 October 2026 when acq_start began writing
 * DMNGT.
 *
 * It then said what was absent was narrower: OVRIE unenabled and OVRMOD
 * unchosen, with no watched flag behind which a count could be honest.
 *
 * RM0455 Rev 13 page 980, read on Tuesday 20 October 2026, raised the stakes
 * rather than settling it: an overrun does not lose one sample, it blocks every
 * further transfer request until software clears the flag. So an unwatched
 * overrun ends the stream, and the console shows a run that goes quiet with no
 * line saying why. The same page also showed OVRMOD needed no choosing: 0 as
 * found is the value it recommends where an overrun is an error.
 *
 * Joseph took the remaining decision the same day and the ground he gave is
 * better than the choice: AN OVERRUN THAT REPORTS ITSELF IS A MEASURED DEFECT,
 * AND ONE THAT SHOWS UP AS A CONSOLE GOING QUIET IS AN UNMEASURED ONE. The
 * timer back end had the pattern already, proven on the board since Saturday
 * 17 October 2026, so adopting it whole makes both back ends report the same
 * failure the same way. acq_start enables OVRIE and ADC_IRQHandler above counts
 * and clears the flag.
 *
 * WHAT A ZERO FROM THIS FUNCTION NOW MEANS, and it is not what -1 meant: the
 * flag was watched for the whole run and never set. The run that reported
 * overruns 0 for 390 blocks on Wednesday 7 October 2026 while nothing watched
 * this flag is why acq.h keeps the two answers separate. */
int acq_conv_overruns(uint32_t *out)
{
    *out = conv_overruns;
    return 0;
}

const char *acq_name(void) { return "dma"; }
