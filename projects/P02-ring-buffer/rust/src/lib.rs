//! P02's single producer, single consumer byte ring in Rust, `no_std`.
//!
//! The same structure, the same invariant and the same four ordering choices as
//! `../../../c/ring/ring.c` and `../../../c/ring/barrier.h`, written
//! independently so the comparison means something.
//!
//! **The invariant**, unchanged from the C because it is a property of the
//! design rather than of the language: the consumer may read a slot only after
//! the producer has published a head that includes it, and the producer may
//! write a slot only after the consumer has published a tail that releases it.
//!
//! **The four modes are a const generic**, so `Ring<0>` through `Ring<3>` are
//! four types the compiler monomorphises, each emitting exactly the barriers it
//! asks for. The C selects with `-DRING_BARRIER` and builds four objects; the
//! C++ uses a template parameter. All three arrive at the same place by
//! different routes, and that is one of the few places in this volume where the
//! route differs enough to be worth a paragraph in the chapter.
//!
//! **What this file honestly does not do, and it is the interesting part.** The
//! C's `ring_t` is one struct with both indices, handed to a producer in thread
//! mode and a consumer in an interrupt handler. That shape is not expressible in
//! safe Rust: two contexts holding `&mut` to one value is exactly what the
//! borrow checker exists to refuse, and it is right to refuse it, because the
//! C's version is sound only by an argument the compiler cannot see.
//!
//! The honest Rust shape is a split: a `Producer` and a `Consumer` handle over
//! one buffer, which needs either `UnsafeCell` behind a safe interface, or one
//! of the `heapless`-style crates that already did that work, or the atomics to
//! cover the data as well as the indices. This crate does none of those: the
//! methods take `&mut self`, so it is a correct sequential ring and the host
//! trace is a fair comparison, and the two-context split is named as missing
//! rather than faked.
//!
//! That is the Rust-specific finding for chapter 2, and it cuts against the easy
//! conclusion. The C compiles a structure whose soundness rests on a prose
//! argument about which context touches which field. Rust will not compile that
//! structure at all without `unsafe`, so the choice is to write the argument
//! down in a type or to opt out of the checking. Neither is free, and saying
//! which one this crate took matters more than the fact that it has no
//! `unsafe` in it.

#![cfg_attr(not(test), no_std)]
#![forbid(unsafe_code)]

use core::sync::atomic::{AtomicU32, Ordering};

/// The capacity the C is built with. A power of two, so the subscript is a
/// mask and no slot is sacrificed to tell full from empty.
pub const RING_SIZE: u32 = 256;
pub const RING_MASK: u32 = RING_SIZE - 1;

const _: () = assert!(
    RING_SIZE & RING_MASK == 0,
    "RING_SIZE must be a power of two"
);
const _: () = assert!(
    RING_SIZE >= 2,
    "a capacity of one leaves no room to be full"
);
const _: () = assert!(
    RING_SIZE <= 0x8000_0000,
    "capacity must stay under half the counter"
);

/// The four ordering choices, by the numbers the C uses for `-DRING_BARRIER`.
pub const BARRIER_NONE: u8 = 0;
pub const BARRIER_COMPILER: u8 = 1;
pub const BARRIER_DMB: u8 = 2;
pub const BARRIER_ACQREL: u8 = 3;

pub fn barrier_name(mode: u8) -> &'static str {
    match mode {
        BARRIER_NONE => "none, not shippable",
        BARRIER_COMPILER => "compiler only, signal fence",
        BARRIER_DMB => "compiler and processor, one DMB",
        BARRIER_ACQREL => "acquire load and release store on the indices",
        _ => "?",
    }
}

/// The ring, in one of four ordering modes.
///
/// `MODE` is a const generic rather than a field, so each mode is its own type
/// and the barriers are chosen at compile time. A runtime field would put a
/// branch in the hot path and measure the branch.
pub struct Ring<const MODE: u8> {
    buf: [u8; RING_SIZE as usize],
    /// Written by the producer only.
    head: AtomicU32,
    /// Written by the consumer only.
    tail: AtomicU32,
    /// Producer only: bytes refused because it was full.
    drops: u32,
}

impl<const MODE: u8> Default for Ring<MODE> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const MODE: u8> Ring<MODE> {
    pub const fn new() -> Self {
        Self {
            buf: [0; RING_SIZE as usize],
            head: AtomicU32::new(0),
            tail: AtomicU32::new(0),
            drops: 0,
        }
    }

    pub fn init(&mut self) {
        *self = Self::new();
    }

    pub fn barrier_name(&self) -> &'static str {
        barrier_name(MODE)
    }

    /// Producer context only. `true` accepted, `false` refused and counted.
    ///
    /// The stale read is safe in one direction only, and that is the argument.
    /// The producer may read a tail older than the truth, believe the buffer
    /// fuller than it is, and refuse a byte it could have taken. That error is
    /// conservative. It cannot write over a slot the consumer has not finished
    /// reading, which is the only outcome that would be a fault.
    pub fn put(&mut self, byte: u8) -> bool {
        let h = self.head.load(Ordering::Relaxed); // mine
        let t = Self::load_other(&self.tail); // theirs
        Self::acquire(); // their tail, then my write

        if h.wrapping_sub(t) >= RING_SIZE {
            self.drops += 1; // policy, counted and not hidden
            return false;
        }

        self.buf[(h & RING_MASK) as usize] = byte; // the data
        Self::release(); // data visible before the index
        Self::store_mine(&self.head, h.wrapping_add(1)); // publish
        true
    }

    /// Consumer context only. Mirror of the above: the consumer may read a head
    /// older than the truth, believe the buffer emptier than it is, and return
    /// early. Also conservative, and also incapable of reading a slot that was
    /// never written.
    pub fn get(&mut self) -> Option<u8> {
        let t = self.tail.load(Ordering::Relaxed); // mine
        let h = Self::load_other(&self.head); // theirs
        Self::acquire(); // their head, then my read

        if h == t {
            return None;
        }

        let byte = self.buf[(t & RING_MASK) as usize];
        Self::release(); // read done before the release
        Self::store_mine(&self.tail, t.wrapping_add(1));
        Some(byte)
    }

    /// Correct across the wrap of the counters, because the subtraction wraps
    /// explicitly. Read this from the consumer: from the producer it is
    /// conservative in one direction only.
    pub fn used(&self) -> u32 {
        self.head
            .load(Ordering::Relaxed)
            .wrapping_sub(self.tail.load(Ordering::Relaxed))
    }

    pub fn drops(&self) -> u32 {
        self.drops
    }

    /// Exposed for the parity comparison, which checks the free-running
    /// counters across their own wrap rather than only the occupancy.
    pub fn head(&self) -> u32 {
        self.head.load(Ordering::Relaxed)
    }

    pub fn tail(&self) -> u32 {
        self.tail.load(Ordering::Relaxed)
    }

    // The four modes, as four pairs of primitives, exactly as barrier.h
    // collapses them in the C. `MODE` is a constant, so each branch folds away.
    fn load_other(v: &AtomicU32) -> u32 {
        if MODE == BARRIER_ACQREL {
            v.load(Ordering::Acquire)
        } else {
            v.load(Ordering::Relaxed)
        }
    }

    fn store_mine(v: &AtomicU32, value: u32) {
        if MODE == BARRIER_ACQREL {
            v.store(value, Ordering::Release);
        } else {
            v.store(value, Ordering::Relaxed);
        }
    }

    fn acquire() {
        if MODE == BARRIER_COMPILER {
            core::sync::atomic::compiler_fence(Ordering::Acquire);
        } else if MODE == BARRIER_DMB {
            core::sync::atomic::fence(Ordering::Acquire);
        }
    }

    fn release() {
        if MODE == BARRIER_COMPILER {
            core::sync::atomic::compiler_fence(Ordering::Release);
        } else if MODE == BARRIER_DMB {
            core::sync::atomic::fence(Ordering::Release);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The operation traces are deliberately NOT here. They live in
    /// `python/tests/test_ring_parity.py`, which drives all four
    /// implementations over one list. What belongs here is the properties that
    /// need no other implementation.
    #[test]
    fn a_fresh_ring_is_empty_and_fills_to_exactly_its_capacity() {
        let mut r: Ring<BARRIER_DMB> = Ring::new();
        assert_eq!(r.get(), None, "a fresh ring is empty");
        for i in 0..RING_SIZE {
            assert!(
                r.put((i & 0xFF) as u8),
                "refused byte {} of {}",
                i,
                RING_SIZE
            );
        }
        assert_eq!(r.used(), RING_SIZE);
        assert!(!r.put(0), "accepted a byte past the capacity");
        assert_eq!(r.drops(), 1, "the refusal was not counted");
        // No slot is sacrificed to tell full from empty, which is what the
        // free-running counters buy over a wrapped index.
        for i in 0..RING_SIZE {
            assert_eq!(
                r.get(),
                Some((i & 0xFF) as u8),
                "byte {} came back wrong",
                i
            );
        }
        assert_eq!(r.get(), None, "a drained ring is empty");
    }

    #[test]
    fn the_counters_stay_correct_across_their_own_wrap() {
        let mut r: Ring<BARRIER_DMB> = Ring::new();
        // Start just below the wrap of the 32-bit counters, which is the case
        // the arithmetic exists for and the one a masked-to-capacity index
        // would get wrong.
        r.head.store(u32::MAX - 3, Ordering::Relaxed);
        r.tail.store(u32::MAX - 3, Ordering::Relaxed);
        for i in 0..16u32 {
            assert!(r.put((i & 0xFF) as u8));
            assert_eq!(r.get(), Some((i & 0xFF) as u8));
            assert_eq!(r.used(), 0, "used must be zero after a matched pair");
        }
        // The counters have wrapped past zero and the occupancy is still right.
        assert!(r.head() < 16, "the head did not wrap");
    }

    #[test]
    fn the_trace_does_not_depend_on_the_ordering_mode() {
        // A single-threaded trace must be identical in all four modes: the
        // barriers constrain visibility between contexts and say nothing about
        // one context's own sequence. If this ever fails, a mode is doing
        // something to the data path rather than to the ordering.
        fn trace<const MODE: u8>() -> ([u8; 300], u32, u32, u32) {
            let mut r: Ring<MODE> = Ring::new();
            let mut out = [0u8; 300];
            let mut at = 0;
            for i in 0..300u32 {
                if i % 3 == 0 {
                    if let Some(b) = r.get() {
                        out[at] = b;
                        at += 1;
                    }
                } else {
                    r.put((i & 0xFF) as u8);
                }
            }
            (out, r.used(), r.drops(), r.head())
        }
        let reference = trace::<BARRIER_NONE>();
        assert_eq!(trace::<BARRIER_COMPILER>(), reference);
        assert_eq!(trace::<BARRIER_DMB>(), reference);
        assert_eq!(trace::<BARRIER_ACQREL>(), reference);
    }

    #[test]
    fn every_mode_names_itself() {
        let r: Ring<BARRIER_NONE> = Ring::new();
        assert_eq!(r.barrier_name(), "none, not shippable");
        let r: Ring<BARRIER_ACQREL> = Ring::new();
        assert_eq!(
            r.barrier_name(),
            "acquire load and release store on the indices"
        );
    }
}
