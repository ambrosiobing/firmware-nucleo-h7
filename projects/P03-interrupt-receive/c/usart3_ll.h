/* projects/P03-interrupt-receive/c/usart3_ll.h: the register-level receive, and every refusal it carries.
 *
 * Register level rather than through a vendor library, deliberately. The two
 * documented failure modes this project reproduces are both consequences of how
 * a library's receive path is written, so a project that used that library could
 * not show either one clearly, and the repair would be a different call rather
 * than a readable sequence.
 *
 * EVERY FUNCTION HERE REFUSES OR IS INERT until RM0455 is read. Four separate
 * things are unconfirmed and each fails differently:
 *
 *   the USART register offsets   a write goes somewhere else; nothing receives
 *   the status flag bits         the handler tests the wrong bit and either
 *                                never reads a byte or never clears an error
 *   the interrupt enable bits    the interrupt never fires, which looks like a
 *                                wiring problem and is not
 *   the NVIC position            the interrupt fires into the wrong handler,
 *                                which looks like a peripheral that does not work
 *
 * The flag clearing procedure is the one worth singling out. On this family a
 * flag is cleared by writing to a dedicated interrupt-flag-clear register, not
 * by reading the status register as older families allowed. The most widely read
 * worked example of this exact problem is written for an older family, so copying
 * it produces a handler that appears correct and never clears anything.
 */
#ifndef USART3_LL_H
#define USART3_LL_H

#include <stdbool.h>
#include <stdint.h>

/* Returns RX_OK, or a negative rx_status_t naming what is unconfirmed. */
int usart3_init_receive(void);

uint32_t usart3_status(void);
bool     usart3_has_byte(uint32_t status);
bool     usart3_has_overrun(uint32_t status);
bool     usart3_has_framing_error(uint32_t status);
bool     usart3_has_noise_error(uint32_t status);

/* Reading the data register is what clears the byte-ready condition. A handler
 * that returns without reading leaves it true and is re-entered immediately. */
uint8_t  usart3_read_byte(void);

/* Each clear is a write to the dedicated register. TO BE CONFIRMED. */
void usart3_clear_overrun(void);
void usart3_clear_framing_error(void);
void usart3_clear_noise_error(void);

/* Only faults.c's first mode uses these. The correct path never disables the
 * receive interrupt at all, which is the whole repair. */
void usart3_enable_receive_interrupt(void);
void usart3_disable_receive_interrupt(void);

#endif /* USART3_LL_H */
