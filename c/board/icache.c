/* c/board/icache.c: the instruction cache, which is ARM's and not ST's.
 *
 * WHY THIS FILE EXISTS, and it is not performance.
 *
 * On Friday 2 October 2026 three cycle figures in this volume moved because of
 * edits that changed no logic at all. Chapter 1's delay loop went from 9 cycles
 * an iteration to 19 when four register writes were added to the reset handler.
 * Chapter 9's encoder came out 7.8 per cent slower after a variable was renamed
 * and three printf lines were added. Chapter 2's four ordering modes differ by up
 * to 12 per cent, and they are four separate images of four different sizes.
 *
 * The common cause is that with no instruction cache every instruction is fetched
 * from flash, and what a loop costs then depends on where the linker happened to
 * put it. A cycle figure measured that way belongs to a build, not to a function,
 * and a difference smaller than about ten per cent between two builds cannot be
 * attributed to the change under test. That is recorded in chapters 2 and 9 and it
 * is the reason chapter 19 stopped being a late chapter and became a prerequisite.
 *
 * WHAT MADE IT REACHABLE NOW. The enable sequence is architectural. Read on
 * Friday 2 October 2026 from ARM's cachel1_armv7.h in the Cube pack, where
 * SCB_EnableICache is: return if already on, then dsb, isb, write ICIALLU, dsb,
 * isb, set CCR.IC, dsb, isb. Every name in it is ARMv7-M. RM0455 governs none of
 * it, which puts the cache in the same category as DWT_CYCCNT and SysTick and
 * makes it usable while the clock tree, the converter and the transfer engine are
 * all still waiting on a manual nobody has read.
 *
 * WHY board_init DOES NOT CALL THIS. Two reasons, and the second is the real one.
 *
 * First, every measurement published in this volume so far was taken with the
 * cache off. Enabling it in board_init would silently invalidate all of them,
 * which is the kind of quiet change this repository exists to avoid.
 *
 * Second, and better: the cache is off at reset, so a single image can measure a
 * function cold, call this, and measure the same function again at the same
 * address. The code has not moved, the build has not changed, nothing has shifted
 * by a byte. The difference between the two figures is therefore the cache and
 * nothing else. Comparing two builds could never say that. A project that wants
 * the comparison does it in one image, and that is what chapter 9 now does.
 *
 * There is no disable here on purpose. cachel1_armv7.h has one, at line 78, but
 * its body has not been read and the order of its invalidate against its clear of
 * CCR.IC is not obvious enough to write from memory. Nothing needs it: the
 * interesting direction is off then on, and reset already provides the off.
 *
 * THE CONSEQUENCE FOR TIMING, which a caller must know. board_delay_calibrate
 * measures the delay loop once, during board_init, with the cache in whatever
 * state it is then. Enabling the cache afterwards makes that loop faster than the
 * calibration says, so board_delay_ms will then run short. Nothing in this
 * repository times anything with board_delay_ms, which is why it is a note rather
 * than a refusal, but a project that enables the cache and then needs an accurate
 * millisecond must call board_delay_calibrate again.
 */
#include "board.h"
#include "stm32h7a3_regs.h"

/* The two barriers the sequence needs, with the memory clobber CMSIS also uses.
 * The clobber is not decoration: it stops the compiler moving an ordinary access
 * across the barrier. The register accesses themselves are volatile and so keep
 * their order relative to each other without it. */
#define DSB() __asm volatile ("dsb" ::: "memory")
#define ISB() __asm volatile ("isb" ::: "memory")

bool board_icache_enabled(void)
{
    /* Read the bit rather than remember having written it, in the style
     * cycles.c arrived at. A write to CCR that did not take looks exactly like
     * one that did if the only evidence is a flag in RAM. */
    return (SCB_CCR & (1u << SCB_CCR_IC_POS)) != 0u;
}

bool board_icache_enable(void)
{
    if (board_icache_enabled()) {
        return true;
    }

    /* ARM's order, and each step earns its place.
     *
     * The invalidate comes first because the cache contents at reset are not
     * defined. Enabling first and invalidating second would allow a window in
     * which the core executes whatever the cache happened to hold, which on a
     * part that has just been reset is nothing anybody has written.
     *
     * The barriers separate the three operations. dsb waits for the memory
     * system to finish; isb flushes the pipeline so that what follows is fetched
     * under the new configuration rather than the old one. Enabling an
     * instruction cache changes how the very next instruction is fetched, so
     * this is one of the few places where isb is load bearing rather than
     * cautious. */
    DSB();
    ISB();
    SCB_ICIALLU = 0u;
    DSB();
    ISB();
    SCB_CCR |= (1u << SCB_CCR_IC_POS);
    DSB();
    ISB();

    return board_icache_enabled();
}
