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
 * WHY THE PAD SIZES ARE WHAT THEY ARE. 0, 64 and 160 bytes. Zero gives the
 * reference build. The other two are not multiples of each other and neither is a
 * multiple of 32, the Cortex-M7 cache line, so if an effect appears only at
 * particular alignments these three are unlikely to all land the same way.
 */
#ifndef P09_PAD_BYTES
#define P09_PAD_BYTES 0
#endif

#if P09_PAD_BYTES > 0
/* `used` because nothing references it and the compiler would otherwise discard
 * it, which would silently turn this experiment into three identical builds: the
 * worst outcome, since the cold figures would then agree and appear to disprove
 * the very effect being tested. */
__attribute__((used, section(".text.p09pad")))
static const unsigned char p09_pad[P09_PAD_BYTES] = { 0 };
#endif

/* A translation unit with no external symbols is legal but draws -Wpedantic on
 * some settings for being empty when P09_PAD_BYTES is 0. This costs one byte of
 * .rodata in the reference build and keeps the file honest in all three. */
const unsigned char p09_pad_bytes = (unsigned char) P09_PAD_BYTES;
