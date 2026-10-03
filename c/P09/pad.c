/* c/P09/pad.c: deliberate displacement, so that placement can be measured rather
 * than assumed.
 *
 * WHAT THIS IS FOR. Four times in two days a cycle figure in this volume moved
 * because of an edit that changed no logic. Chapter 1's delay loop, chapter 9's
 * encoder twice, and chapter 2's four ordering modes. The explanation offered each
 * time was that with no instruction cache every instruction is fetched from flash,
 * so what a loop costs depends on where the linker put it.
 *
 * That explanation was never tested. It was inferred from four accidents, and an
 * accident is not an experiment: in every one of those cases the code changed as
 * well as moving, so a reader is entitled to ask which of the two mattered.
 *
 * This file moves the code WITHOUT changing it. It contributes nothing but a run
 * of zero bytes into the code section, and because it is listed first among the
 * sources, every instruction in main.c and payload.c lands that many bytes later.
 * The encoder is byte for byte identical across the variants. Only its address
 * differs.
 *
 * WHAT THE EXPERIMENT ASKS. Each variant reports the encoder cold and cached, both
 * measured at one address in one build. Two predictions follow from the
 * explanation above, and they are separable:
 *
 *   the COLD figures should differ between variants, because fetch cost depends on
 *   where the loop sits relative to whatever granule the flash interface fetches;
 *
 *   the CACHED figures should agree, because once the loop is resident in the
 *   cache its address stops mattering.
 *
 * If both hold, every cycle comparison in this volume can be made with the cache
 * on and reported as a property of the code. If the cached figures also differ, the
 * explanation is wrong or incomplete and four chapters that lean on it need
 * revisiting, which would be the more valuable outcome and is the reason for
 * running it rather than assuming.
 *
 * WHY ZERO BYTES ARE SAFE. The array is never called and never read. It occupies a
 * .text subsection so the linker places it with the code, which is the whole point,
 * but nothing branches into it. Its contents are irrelevant; only its length is.
 *
 * WHY THE PAD SIZES ARE WHAT THEY ARE, after the first set was useless.
 *
 * The first attempt used 0, 64 and 160 bytes, with a comment claiming none of
 * them was a multiple of 32. Both are: 64 is two cache lines and 160 is five. The
 * three images therefore had identical alignment modulo 32 and the figures came
 * out identical to the hundredth of a cycle in all three. That is exactly what the
 * placement hypothesis predicts under a whole-line shift, so the experiment could
 * not fail and settled nothing. The arithmetic was asserted and not checked.
 *
 * The set is now 0, 4, 12, 20 and 36 bytes. 4, 12 and 20 are three distinct
 * residues modulo 32, so if cost depends on where a loop sits within a fetch
 * granule these three cannot all land the same way.
 *
 * 36 is the control, and it is the most informative of the five. It has the same
 * residue as 4, being 32 + 4, but sits a whole line further along. If 4 and 36
 * agree with each other while differing from 0, 12 and 20, then the residue is
 * what matters and the explanation is alignment within a granule. If 36 instead
 * tracks its distance rather than its residue, something other than alignment is
 * at work and the explanation needs rebuilding from the start.
 *
 * WHAT THE FIRST SET DID ESTABLISH, which is worth keeping: the measurement
 * repeats to the hundredth of a cycle across separate flashes and board resets,
 * and a uniform displacement by whole cache lines costs nothing. Both are real
 * results. Neither is the one being asked for.
 */
#ifndef P09_PAD_BYTES
#define P09_PAD_BYTES 0
#endif

#if P09_PAD_BYTES > 0
/* TWO things keep this array, and the first attempt had only one of them.
 *
 * `used` stops the COMPILER discarding a static nothing references. That is not
 * enough: this build links with --gc-sections, and the LINKER removes an
 * unreferenced section regardless of any compiler attribute. The first run of this
 * experiment produced three images with .text at 8476 bytes each, identical, and
 * had the cold figures been read from them they would have agreed and appeared to
 * disprove the effect under test.
 *
 * The other half is KEEP(*(.text.p09pad)) in c/ld/stm32h7a3zi.ld, which also
 * places this section before *(.text) so that the displacement does not depend on
 * the order of objects on the link line. The linker script's own comment, four
 * lines above where that KEEP now sits, had already explained this about the
 * vector table. */
__attribute__((used, section(".text.p09pad")))
static const unsigned char p09_pad[P09_PAD_BYTES] = { 0 };
#endif

/* A translation unit with no external symbols is legal but draws -Wpedantic on
 * some settings for being empty when P09_PAD_BYTES is 0. This costs one byte of
 * .rodata in the reference build and keeps the file honest in all three. */
const unsigned char p09_pad_bytes = (unsigned char) P09_PAD_BYTES;
