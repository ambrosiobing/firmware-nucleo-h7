/* c/ld/stm32h7a3zi.ld: the linker script for the STM32H7A3ZI.
 *
 * P01 owns this file and every other project links against it.
 *
 * THE ONE THING THAT MAKES MOST PUBLISHED SCRIPTS WRONG FOR THIS PART.
 * The STM32H7A3 is documented by RM0455. The popular member of the family, the
 * STM32H743, is documented by RM0433, and the two have different memory maps.
 * A script copied from an H743 project does not merely waste memory: it places
 * sections in regions that are the wrong size or, for the second and low-power
 * domains, at addresses that mean something different. Every length below is
 * this part's, taken from two independent machine-readable sources that name
 * this exact board and agree exactly.
 *
 * Base addresses are architectural. The lengths are what differ between family
 * members, and these are the ones to check if anything here is ever doubted:
 *
 *   Flash                   0x08000000   2048 kB   dual bank
 *   DTCM                    0x20000000    128 kB   unreachable by the main engines
 *   AXI SRAM                0x24000000   1024 kB   three banks, see below
 *   Second domain SRAM      0x30000000    128 kB   two banks of 64 kB
 *   Low-power domain SRAM   0x38000000     32 kB   survives while others sleep
 *
 * That is 1312 kB of declared SRAM. The commonly repeated "about 1.4 MB" is
 * only reached by adding the tightly coupled instruction memory, whose size
 * neither source declares, so it is not declared here either.
 */

ENTRY(Reset_Handler)

MEMORY
{
  /* Dual bank, and the split is a design choice rather than a fixed boundary.
     Declared whole here; a project that wants A and B firmware slots carves it
     up in its own script rather than changing this one. */
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K

  /* Tightly coupled, zero wait state, and the fastest memory on the part for
     the stack and for anything the processor touches constantly.
     THE TRAP: the main transfer engines cannot reach it at all. A buffer the
     linker placed here and a transfer engine was pointed at produces a transfer
     that never completes rather than an error. That is P19's subject, and it is
     why .dma_buffers below is deliberately NOT in this region. */
  DTCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K

  /* The general-purpose pool, reachable by the main engines. Three banks, and
     they are contiguous, so one region of 1024K is correct and simpler than
     three. Split it only if a project needs bank-level placement. */
  AXI    (rwx) : ORIGIN = 0x24000000, LENGTH = 1024K

  /* Second domain. Two banks of 64 kB, the second at 0x30010000. */
  SRAM2  (rwx) : ORIGIN = 0x30000000, LENGTH = 128K

  /* Low-power domain. Survives while the other domains sleep, which is the
     whole reason the third transfer engine exists. P07 and P10 use this. */
  SRAMLP (rwx) : ORIGIN = 0x38000000, LENGTH = 32K
}

/* The stack goes at the top of DTCM and grows down. Tightly coupled memory is
   the right place for it: zero wait states, and nothing else needs to reach it.
   One kilobyte of heap is reserved only so that a library call which assumes
   one does not fail silently; nothing in this volume calls malloc, and the
   assertion below fails the link if the two ever collide. */
_stack_size = 8K;
_heap_size  = 1K;

SECTIONS
{
  /* The vector table first, at the start of flash, because the processor reads
     the initial stack pointer and the reset vector from the first eight bytes
     at boot. KEEP, or garbage collection removes a table nothing references. */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);

    /* Chapter 9's placement experiment, and it needs BOTH halves of this.
     *
     * KEEP, for the reason given above .isr_vector: the padding array is
     * referenced by nothing, so --gc-sections removes it. The compiler attribute
     * `used` does not help, because that only stops the COMPILER discarding it.
     * Without this line the three padded images came out byte for byte identical,
     * with .text at 8476 in all three, and the experiment silently became three
     * copies of one build whose agreeing figures would have looked like a result.
     *
     * FIRST, before *(.text), because the point is to displace everything after
     * it. Putting it later in this list would keep the bytes and move nothing,
     * which is the same failure wearing a different hat. Placing it here means the
     * displacement does not depend on the order of objects on the link line. */
    KEEP(*(.text.p09pad))

    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  /* Unwinding tables. Kept even with exceptions disabled, because a library
     object may still carry them and a dangling reference fails the link. */
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .);
                     KEEP(*(.preinit_array*))
                     PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array    : { PROVIDE_HIDDEN(__init_array_start = .);
                     KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*))
                     PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .);
                     KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*))
                     PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH

  /* Initialised data: lives in DTCM at run time, and its initial image is in
     flash. startup.c copies between _sidata and _sdata.._edata. */
  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > DTCM AT > FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > DTCM

  /* Buffers a transfer engine must reach go here, in AXI SRAM, NOT in DTCM.
     Use it with __attribute__((section(".dma_buffers"))). The alignment is 32
     bytes, one cache line on this core, so that a cache maintenance operation
     on one buffer cannot touch a neighbour. P19 is the chapter that explains
     why both of those sentences matter. */
  .dma_buffers (NOLOAD) :
  {
    . = ALIGN(32);
    _sdma = .;
    *(.dma_buffers)
    *(.dma_buffers*)
    . = ALIGN(32);
    _edma = .;
  } > AXI

  /* Named regions for the two other domains, unused until P07 and P10. Declared
     so that placing something there is a one-line attribute rather than an edit
     to this file. */
  .sram2 (NOLOAD) : { . = ALIGN(4); *(.sram2) *(.sram2*) . = ALIGN(4); } > SRAM2
  .sramlp (NOLOAD) : { . = ALIGN(4); *(.sramlp) *(.sramlp*) . = ALIGN(4); } > SRAMLP

  /* The heap and the stack, and the check that they do not meet. The assertion
     is the point of this block: without it, a stack that has grown into the
     heap is discovered by a crash at run time rather than by a failed link. */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _heap_size;
    . = . + _stack_size;
    . = ALIGN(8);
  } > DTCM

  _estack = ORIGIN(DTCM) + LENGTH(DTCM);

  /* NO /DISCARD/ SECTION HERE, deliberately. Scripts for this family commonly
     carry `/DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }`, copied from one
     generated script to the next. It throws away every section of those three
     archives, and printf, memcpy and the integer helpers come from exactly
     there. Where it appears to work it is because garbage collection had already
     removed what was unused, which makes it a line that does nothing on a good
     day and breaks the link on a bad one. --gc-sections above does the real
     work. */

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

ASSERT(_estack - _ebss > _stack_size + _heap_size,
       "DTCM is too small for .data, .bss, the heap and the stack together")
