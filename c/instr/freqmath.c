/* c/instr/freqmath.c: see freqmath.h for why this is not inside freqcount.c.
 *
 * Nothing here reads a register, a clock or a global. Every function is a pure
 * function of its arguments, which is what lets a host drive it.
 */
#include "freqmath.h"

uint32_t freqmath_compose(uint32_t wraps, uint32_t counter)
{
    /* The counter is LPTIM1's 16 bits whatever else arrives here, so it is
     * masked rather than trusted. A caller that passed a wider value would
     * otherwise corrupt the wrap count silently. */
    return (wraps << 16) | (counter & 0xFFFFu);
}

uint32_t freqmath_edges(uint32_t before, uint32_t after)
{
    /* Modular at 32 bits, which unsigned subtraction gives for free and which is
     * the reason this is unsigned. Writing it as a signed difference and taking
     * the absolute value would be wrong in the one case it exists for. */
    return after - before;
}

uint32_t freqmath_usable_max_hz(uint32_t arr, uint32_t tick_hz)
{
    if (arr == 0u || tick_hz == 0u) {
        return 0u;
    }
    /* arr + 1 distinct counter values per tick. The product is computed in 64
     * bits and clamped, because 0xFFFFFFFF + 1 times any rate leaves 32 bits and
     * an arr that large is a caller's mistake rather than a configuration. */
    const uint64_t max = ((uint64_t) arr + 1u) * (uint64_t) tick_hz;
    if (max > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t) max;
}

uint64_t freqmath_mhz(uint32_t edges, uint32_t tick_hz, uint32_t ticks,
                      uint32_t ceiling_hz, uint32_t arr)
{
    if (tick_hz == 0u || ticks == 0u) {
        return 0u;
    }

    /* Multiply first and divide last. edges times the tick rate times a thousand
     * over the tick count: at 1 kHz over a one second gate that is 1000 times
     * 256 times 1000 over 256, and dividing edges by ticks first would give 3
     * and then 768000 millihertz instead of 1000000. The intermediate reaches
     * about 1.7e10 at the counter's full width, so it is 64 bit. */
    const uint64_t mhz = ((uint64_t) edges * (uint64_t) tick_hz * 1000u)
                       / (uint64_t) ticks;

    const uint64_t hz = mhz / 1000u;

    /* The sampling ceiling, when the caller knows one. At or above it, edges
     * were missed rather than counted. */
    if (ceiling_hz != 0u && hz >= (uint64_t) ceiling_hz) {
        return 0u;
    }

    /* The usable maximum, which is lower and is about the wrap poll rather than
     * the sampling. Derived rather than passed, so a different autoreload or
     * tick rate moves it without a caller having to remember. */
    const uint32_t usable = freqmath_usable_max_hz(arr, tick_hz);
    if (usable != 0u && hz >= (uint64_t) usable) {
        return 0u;
    }

    return mhz;
}

uint32_t freqmath_ticks_for_ms(uint32_t gate_ms, uint32_t tick_hz)
{
    if (gate_ms == 0u || tick_hz == 0u) {
        return 0u;
    }
    /* 64 bit intermediate so that a long gate cannot wrap the product. A gate of
     * an hour at 256 Hz is 921600 ticks, which fits in 32, but the multiply
     * before the divide is what would overflow and the cast is free. */
    const uint64_t ticks = ((uint64_t) gate_ms * (uint64_t) tick_hz) / 1000u;
    if (ticks == 0u) {
        /* A gate shorter than one tick still gets one tick, which is longer than
         * asked for. The caller is told the truth by the tick count that comes
         * back from the measurement, which is why that field is reported. */
        return 1u;
    }
    if (ticks > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t) ticks;
}

uint64_t freqmath_resolution_mhz(uint32_t gate_ms)
{
    if (gate_ms == 0u) {
        return 0u;
    }
    return 1000000u / (uint64_t) gate_ms;
}
