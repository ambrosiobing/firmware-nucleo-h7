/* marker.h: the pin the witness watches. See marker.c for which pin and why
 * that choice is still marked as unconfirmed. */
#ifndef MARKER_H
#define MARKER_H

void marker_init(void);
void marker_high(void);
void marker_low(void);

/* A high then immediately low. Used by the builds where the sample instant is
 * a point rather than an interval. The witness sees the rising edge; the fall
 * is only there to arm the next one. */
void marker_pulse(void);

#endif /* MARKER_H */
