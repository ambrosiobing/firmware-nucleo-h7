/* cyccnt.c: the cheapest instrument on the board, with a self-test.
 *
 * The chapter's rule is that the wrapper does not assume the core's cycle
 * counter works. It enables it, checks that it moved, and reports which
 * backend it ended up with. A wrapper that silently returned zeros would make
 * every timing figure in the book wrong in the same invisible way.
 */
#include "cyccnt.h"

#include "stm32h7xx.h"

/* The debug block's lock access register. Some Cortex-M7 implementations want
 * this key before the trace registers accept writes; on others it reads as
 * zero and the write is harmless. Do not settle which applies from a forum
 * post. The self-test below settles it. */
#define DWT_LAR      (*(volatile uint32_t *) 0xE0001FB0u)
#define DWT_LAR_KEY  0xC5ACCE55u

static cyccnt_backend_t backend = CYCCNT_NONE;
static uint32_t tick_hz;

/* The fallback. TIM2 is one of only two 32-bit timers on this part, which is
 * why chapter 20 is careful about who owns it: an RTOS build cannot have this
 * and a 32-bit application timer at the same time. Prescaler at 1 so it counts
 * the timer clock directly, reload at maximum so it free runs for about 15
 * seconds at 280 MHz before it wraps. Any interval longer than that must be
 * measured a different way. */
static int timer_backend_init(void)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    (void) RCC->APB1LENR;

    TIM2->CR1  = 0u;
    TIM2->PSC  = 0u;             /* divide by 1 */
    TIM2->ARR  = 0xFFFFFFFFu;
    TIM2->CNT  = 0u;
    TIM2->EGR  = TIM_EGR_UG;     /* load the prescaler now */
    TIM2->CR1  = TIM_CR1_CEN;

    uint32_t a = TIM2->CNT;
    for (volatile int i = 0; i < 64; i++) { }
    if (TIM2->CNT == a) {
        return 0;
    }

    /* TO BE CONFIRMED against RM0455: the APB1 timer clock for this part, and
     * whether the timer clock is the bus clock or twice it, which depends on
     * the prescaler the clock tree was left with. Getting this wrong scales
     * every measured time by exactly two, which is the kind of error that
     * looks plausible and is not. Until it is read, tick_hz is reported as
     * zero so a caller cannot convert ticks to seconds by accident. */
    tick_hz = 0u;
    return 1;
}

cyccnt_backend_t cyccnt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_LAR = DWT_LAR_KEY;
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t a = DWT->CYCCNT;
    for (volatile int i = 0; i < 64; i++) { }
    if (DWT->CYCCNT != a) {
        backend = CYCCNT_DWT;
        tick_hz = SystemCoreClock;   /* the counter counts core clocks */
        return backend;
    }

    backend = timer_backend_init() ? CYCCNT_TIMER : CYCCNT_NONE;
    return backend;
}

uint32_t cyccnt_now(void)
{
    switch (backend) {
    case CYCCNT_DWT:   return DWT->CYCCNT;
    case CYCCNT_TIMER: return TIM2->CNT;
    default:           return 0u;
    }
}

uint32_t cyccnt_tick_hz(void) { return tick_hz; }

const char *cyccnt_backend_name(void)
{
    switch (backend) {
    case CYCCNT_DWT:   return "core cycle counter";
    case CYCCNT_TIMER: return "32-bit timer, tick rate unconfirmed";
    default:           return "none, timing not measured";
    }
}
