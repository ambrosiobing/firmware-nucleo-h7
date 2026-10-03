/* projects/P03-interrupt-receive/c/rx.h: one receive interface, three paths behind it.
 *
 * The same shape as P06's acq.h, and for the same reason: the application above
 * this header is byte identical across all three builds, selected with
 * -DRX_PATH=polled|flag|ring, so the comparison is of mechanisms rather than of
 * three programs. A main.c that knew which path it was talking to would make the
 * results table meaningless.
 *
 * THE THREE PATHS, simplest first.
 *
 *   polled  the main loop reads the data register when the flag says a byte is
 *           there. Loses bytes the moment the loop is slower than the line,
 *           which is the point of including it.
 *   flag    the interrupt stores one byte and sets a flag. Loses a byte whenever
 *           a second arrives before the loop has taken the first, which at any
 *           real rate is immediately.
 *   ring    the interrupt pushes into P02's ring buffer. The one that keeps the
 *           bytes, and the producer half of P02's structure.
 *
 * WHAT NONE OF THEM DO YET. Every path returns a negative value from rx_start
 * rather than running with a guessed peripheral setting. The USART register
 * offsets, the flag bits, the interrupt enable bits and the NVIC position are
 * all RM0455's and none has been read. A receive path configured from
 * recollection of a sibling part does not fail cleanly: it either receives
 * nothing, or receives rubbish that looks like a line-rate mismatch.
 */
#ifndef RX_H
#define RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Why rx_start refused, so the console names the document to open. */
typedef enum {
    RX_OK                     =  0,
    RX_ERR_USART_UNCONFIRMED  = -1,  /* RM0455 registers, flags, interrupt bits */
    RX_ERR_NVIC_UNCONFIRMED   = -2,  /* RM0455 interrupt position for USART3 */
    RX_ERR_PINS_UNCONFIRMED   = -3,  /* MB1363 virtual COM port pins */
    RX_ERR_CLOCK_UNCONFIRMED  = -4,  /* the baud divider needs the clock */
} rx_status_t;

/* Everything the measurement needs, counted on the target.
 *
 * THE DISTINCTION THAT MAKES THE MEASUREMENT MEAN ANYTHING. `overruns` is the
 * peripheral telling us it had a byte and we did not read it in time, which is
 * loss inside the target. `dropped` is the ring refusing a byte because it was
 * full, which is also loss inside the target but at a different layer. Bytes
 * that never reach the peripheral at all are lost in the bridge between the host
 * and the board, and no counter here can see them: that is why the host must
 * compare what it sent against `accepted` and attribute the difference, and why
 * reporting a single "bytes lost" figure is the easiest way to publish a wrong
 * conclusion. The most common finding, that a link fails above some rate, is
 * usually a statement about the bridge.
 */
typedef struct {
    uint32_t accepted;      /* bytes the path handed to the application */
    uint32_t overruns;      /* peripheral had a byte we did not read in time */
    uint32_t dropped;       /* the ring was full; always 0 for polled and flag */
    uint32_t framing;       /* framing errors, which are a rate mismatch */
    uint32_t noise;         /* noise errors, which are a cable or a rate problem */
    uint32_t handler_calls; /* how often the interrupt ran, 0 for polled */
    uint32_t peak_used;     /* peak ring occupancy; 0 for polled and flag */
} rx_stats_t;

/* Arm the receive path. Returns RX_OK or a negative rx_status_t. */
int rx_start(void);

/* Take one byte if the path has one. Returns 1 when *b is set, 0 when empty. */
int rx_get(uint8_t *b);

/* The counters, read-only. Safe to call from the main loop only. */
const rx_stats_t *rx_stats(void);

/* Which path this build selected, for the status line and for the results
 * table. Matches the RX_PATH argument exactly, so the firmware and the host
 * cannot disagree about what was running. */
const char *rx_name(void);

/* The two documented failure modes, in faults.c, enabled only by an explicit
 * build flag. They exist to be reproduced on purpose and then repaired, because
 * a repair is only convincing once the symptom has been seen. */
bool rx_fault_injection_enabled(void);
const char *rx_fault_name(void);

#endif /* RX_H */
