/* marker.h: the pin the witness watches. See marker.c for which pin, the
 * citation that settled it, and why it toggles rather than pulses. */
#ifndef MARKER_H
#define MARKER_H

void marker_init(void);
void marker_high(void);
void marker_low(void);

/* One edge per call, alternating high and low. Every sampling instant puts
 * exactly one edge on the pin, and the witness counts both polarities. A
 * pulse of two consecutive stores stood here until Friday 9 October 2026 and
 * was invisible to a 10 microsecond sampler; see marker.c. */
void marker_toggle(void);

#endif /* MARKER_H */
