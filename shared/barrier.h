/* shared/barrier.h: the four ordering choices, selected at build time.
 *
 * P02's deliverable is the argument for where these go, so all four are
 * buildable and the cost of each is a measured number rather than a belief.
 * Select with -DRING_BARRIER=N:
 *
 *   0  none. Not shippable. It exists so the cost of correctness is a number.
 *   1  compiler only. A signal fence. Correct when the counterpart is code on
 *      this same core, because exception entry and return are context
 *      synchronising, so a handler cannot observe a partially retired program
 *      order. Not correct when the counterpart is a transfer engine.
 *   2  compiler and processor. One DMB. Correct in every case in this volume,
 *      including a transfer engine as the counterpart. This is the default.
 *   3  the index accesses carry the ordering themselves. Clearest to read and
 *      what the C11 correctness argument is written against.
 *
 * What is deliberately not in that list is `volatile` on its own. It appears in
 * most published versions of this structure and it is not sufficient: it orders
 * volatile accesses against each other and says nothing about the ordinary
 * store to the data array that has to be visible before the index publishing
 * it. The C standard gives no ordering between a volatile and a non-volatile
 * access, and a compiler may sink that store past the volatile one.
 *
 * The indices are therefore plain uint32_t, not volatile. Marking them volatile
 * and also using the atomic builtins on them is something the compiler may
 * treat as undefined; pick one.
 *
 * Mode 3 differs in shape from 0, 1 and 2: the ordering attaches to the load and
 * the store rather than sitting between them as a fence. Rather than write two
 * function bodies, ring.c is written against four macros that collapse to the
 * right thing in every mode. The chapter's own listing used commented-out
 * alternatives, which cannot be built or measured and so cannot be compared.
 */
#ifndef BARRIER_H
#define BARRIER_H

#ifndef RING_BARRIER
#define RING_BARRIER 2
#endif

#if RING_BARRIER == 0

#define RING_BARRIER_NAME   "none, not shippable"
#define RING_LOAD_OTHER(p)   (*(p))
#define RING_STORE_MINE(p,v) (*(p) = (v))
#define RING_ACQUIRE()       ((void) 0)
#define RING_RELEASE()       ((void) 0)

#elif RING_BARRIER == 1

#define RING_BARRIER_NAME   "compiler only, signal fence"
#define RING_LOAD_OTHER(p)   (*(p))
#define RING_STORE_MINE(p,v) (*(p) = (v))
#define RING_ACQUIRE()       __atomic_signal_fence(__ATOMIC_ACQUIRE)
#define RING_RELEASE()       __atomic_signal_fence(__ATOMIC_RELEASE)

#elif RING_BARRIER == 2

#define RING_BARRIER_NAME   "compiler and processor, one DMB"
#define RING_LOAD_OTHER(p)   (*(p))
#define RING_STORE_MINE(p,v) (*(p) = (v))
#define RING_ACQUIRE()       __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define RING_RELEASE()       __atomic_thread_fence(__ATOMIC_RELEASE)

#elif RING_BARRIER == 3

#define RING_BARRIER_NAME   "acquire load and release store on the indices"
#define RING_LOAD_OTHER(p)   __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define RING_STORE_MINE(p,v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define RING_ACQUIRE()       ((void) 0)
#define RING_RELEASE()       ((void) 0)

#else
#error "RING_BARRIER must be 0, 1, 2 or 3. See shared/barrier.h."
#endif

#endif /* BARRIER_H */
