/* c/board/startup.c: the vector table and the reset handler, in C.
 *
 * P01 owns this file and every other project links it. Written in C rather than
 * assembly on purpose: the deliverable of P01 is a repository whose every byte
 * you can account for, and a table of function pointers is easier to account for
 * than a page of directives.
 *
 * Nothing here is part-specific in a way RM0455 governs. The vector table layout
 * below the first sixteen entries is, and those entries are named from this
 * part's own interrupt list rather than copied from the STM32H743, which has a
 * different one. Where this file lists a device interrupt it is because some
 * project in this volume uses it; the rest are left as reserved slots rather
 * than invented.
 *
 * NEVER COMPILED. There is no arm-none-eabi-gcc on win11 aquamarine as of
 * Thursday 1 October 2026, so this file is written and left honest rather than
 * claimed to work.
 */
#include <stdint.h>

/* From the linker script. */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

/* _estack is an address, not a function, and the first vector table entry is the
 * initial stack pointer rather than a handler. Writing it as
 * (void (*)(void)) &_estack converts an object pointer to a function pointer,
 * which ISO C forbids and -Wpedantic reports. Declaring the symbol as a function
 * lets the table hold it with no cast at all: the linker supplies an address
 * either way, and nothing ever calls it. */
extern void _estack(void);

/* The C library's own initialisation, and ours. */
extern int main(void);
extern void SystemInit(void);

/* Constructors, if any object carries them. */
extern void (*__preinit_array_start[])(void);
extern void (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Every handler is a weak alias for Default_Handler, so a project defines only
 * the ones it uses and an unexpected interrupt still lands somewhere
 * identifiable rather than running into whatever follows in flash. */
#define WEAK_HANDLER(name) \
    void name(void) __attribute__((weak, alias("Default_Handler")))

/* The sixteen architectural entries. These are Cortex-M7 and not part-specific. */
WEAK_HANDLER(NMI_Handler);
WEAK_HANDLER(HardFault_Handler);
WEAK_HANDLER(MemManage_Handler);
WEAK_HANDLER(BusFault_Handler);
WEAK_HANDLER(UsageFault_Handler);
WEAK_HANDLER(SVC_Handler);
WEAK_HANDLER(DebugMon_Handler);
WEAK_HANDLER(PendSV_Handler);
WEAK_HANDLER(SysTick_Handler);

/* Device interrupts this volume actually uses. Each is at the position RM0455
 * gives it, and the positions are TO BE CONFIRMED against that manual: the
 * numbers below follow this family's published list and have not been read from
 * RM0455 directly. A wrong position sends an interrupt to the wrong handler,
 * which presents as a peripheral that appears not to fire.
 *
 * Until they are confirmed, the vector table below places only the handlers at
 * indices this file is certain of and leaves the rest as Default_Handler, which
 * is why an unconfirmed position cannot silently misroute: the project that
 * needs one reads the manual, adds the entry, and says so in its README. */
WEAK_HANDLER(USART3_IRQHandler);          /* P01's console, P03, P04, P05 */
WEAK_HANDLER(TIM2_IRQHandler);            /* P02's fallback counter, P06 */
WEAK_HANDLER(TIM3_IRQHandler);            /* P06's acquisition trigger */
WEAK_HANDLER(ADC_IRQHandler);             /* P06 */
WEAK_HANDLER(EXTI15_10_IRQHandler);       /* the user button on PC13 */

/* The table itself. KEEP in the linker script stops garbage collection removing
 * it, because nothing in C references it. */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    _estack,                     /* initial stack pointer, read by the core   */
    Reset_Handler,               /* reset vector, read by the core            */
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,                  /* reserved */
    SVC_Handler,
    DebugMon_Handler,
    0,                           /* reserved */
    PendSV_Handler,
    SysTick_Handler,
    /* Device interrupts begin here. Positions are TO BE CONFIRMED against
     * RM0455's interrupt and exception vector table before any of these is
     * relied upon. Leaving them out entirely would be worse: an interrupt that
     * fires with no entry runs into whatever the linker put next. */
};

/* A fault that reaches here has no handler, and spinning is the right answer:
 * it leaves the processor in a state a debugger can inspect, with the faulting
 * context still on the stack. Returning or resetting would destroy exactly the
 * evidence somebody needs. */
void Default_Handler(void)
{
    for (;;) {
        __asm volatile ("nop");
    }
}

void Reset_Handler(void)
{
    /* The order here is not negotiable and each step depends on the one before.
     *
     * 1. Copy initialised data from its image in flash to its home in DTCM.
     * 2. Zero the uninitialised data.
     * 3. Only then call anything that might touch a global, which includes the
     *    clock configuration and every constructor.
     *
     * Calling SystemInit before step 2 is a common and subtle fault: it works
     * until SystemInit gains a static variable, and then it works until the
     * value it expected to be zero happens not to be. */

    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }

    /* The clock tree, the flash latency and the voltage scaling. This refuses
     * rather than guessing, see system.c. */
    SystemInit();

    for (void (**f)(void) = __preinit_array_start; f < __preinit_array_end; f++) {
        (*f)();
    }
    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) {
        (*f)();
    }

    (void) main();

    /* main returning is a defect in every project here, since each ends in an
     * infinite loop. Spin rather than fall off the end of flash. */
    for (;;) {
        __asm volatile ("nop");
    }
}
