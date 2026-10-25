/* marker.h: the pin the witness watches. See marker.c for which pin, the
 * citation that settled it, and why it toggles rather than pulses. */
#ifndef MARKER_H
#define MARKER_H

void marker_init(void);
void marker_high(void);
void marker_low(void);

/* The same pin, handed to a timer channel instead of to this file's software
 * toggle: output, push-pull, no pull, fastest slew, alternate function 2, which
 * is TIM3 channel 1 on this pin.
 *
 * WHO CALLS IT AND WHY NOT A BACK END. tim3_bring_up() calls this, in the same
 * function that configures the channel, because the pad and the channel are one
 * mechanism and separating them produces this volume's favourite failure: every
 * register reads back correctly and the pin carries nothing. A back end asks for
 * the timer and gets the pin with it.
 *
 * The tick build does not use this. It keeps marker_init() and marker_toggle()
 * below, because the processor is in the path of its timing by design and a
 * marker that left the processor out would measure a cadence it does not have. */
void marker_init_tim3_ch1(void);

/* One edge per call, alternating high and low. Every sampling instant puts
 * exactly one edge on the pin, and the witness counts both polarities. A
 * pulse of two consecutive stores stood here until Friday 9 October 2026 and
 * was all but invisible to a 10 microsecond sampler; see marker.c. */
void marker_toggle(void);

#endif /* MARKER_H */
