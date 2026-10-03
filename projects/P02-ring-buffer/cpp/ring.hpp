// projects/P02-ring-buffer/cpp/ring.hpp: the ring and its four orderings, in C++17.
//
// The same structure and the same invariant as `../../../c/ring/ring.c`, and the
// same four ordering choices as `../../../c/ring/barrier.h`, written
// independently so the comparison means something.
//
// THE INVARIANT, unchanged from the C because it is a property of the design
// rather than of the language: the consumer may read a slot only after the
// producer has published a head that includes it, and the producer may write a
// slot only after the consumer has published a tail that releases it.
//
// WHAT C++ CHANGES, and it is more here than in P08. The C selects its ordering
// with -DRING_BARRIER and builds four separate objects, so a reader comparing
// two modes compares two builds. Here the mode is a template parameter, so all
// four exist in one translation unit, can be instantiated side by side, and the
// compiler still emits exactly the instructions each one asks for because the
// parameter is a constant. That is the one place in this volume where the C++
// genuinely offers something the C cannot: four variants of one algorithm
// without four builds and without the preprocessor.
//
// It does not change what gets measured. On the board each mode is still its own
// image, because a cycle count wants one image doing one thing.
//
// WHAT IS DELIBERATELY NOT HERE: `volatile`. It appears in most published
// versions of this structure and is not sufficient. It orders volatile accesses
// against each other and says nothing about the ordinary store to the data array
// that must be visible before the index publishing it. The standard gives no
// ordering between a volatile and a non-volatile access.
//
// Single producer and single consumer, and single-threaded in this header's own
// use: the methods take `*this` by mutable reference, so the host trace is
// sequential. A genuine two-context split, a producer handle and a consumer
// handle over one buffer, is a different shape in every language and is the
// subject of the board half rather than of this file.
//
// No exceptions, no RTTI, no heap, no iostream.
#ifndef P02_RING_HPP
#define P02_RING_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace p02 {

// The capacity the C is built with. A power of two, so the subscript is a mask
// and no slot is sacrificed to tell full from empty.
inline constexpr std::uint32_t ring_size = 256;
inline constexpr std::uint32_t ring_mask = ring_size - 1;

static_assert((ring_size & ring_mask) == 0, "ring_size must be a power of two");
static_assert(ring_size >= 2, "a capacity of one leaves no room to be full");
static_assert(ring_size <= 0x80000000u, "capacity must stay under half the counter");

enum class Barrier : int {
    // Not shippable. It exists so the cost of correctness is a number.
    None = 0,
    // A signal fence. Correct when the counterpart is code on this same core,
    // because exception entry and return are context synchronising. Not
    // correct when the counterpart is a transfer engine.
    Compiler = 1,
    // One DMB. Correct in every case in this volume, a transfer engine
    // included. This is the C's default.
    Dmb = 2,
    // The index accesses carry the ordering themselves. Clearest to read, and
    // what the correctness argument is written against.
    AcqRel = 3,
};

constexpr const char* name(Barrier b)
{
    switch (b) {
    case Barrier::None:     return "none, not shippable";
    case Barrier::Compiler: return "compiler only, signal fence";
    case Barrier::Dmb:      return "compiler and processor, one DMB";
    case Barrier::AcqRel:   return "acquire load and release store on the indices";
    }
    return "?";
}

template <Barrier B>
class Ring {
public:
    void init()
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        drops_ = 0;
        buf_.fill(0);
    }

    // Producer context only.
    //
    // The stale read is safe in one direction only, and that is the argument.
    // The producer may read a tail older than the truth, believe the buffer
    // fuller than it is, and refuse a byte it could have taken. That error is
    // conservative. It cannot write over a slot the consumer has not finished
    // reading, which is the only outcome that would be a fault.
    bool put(std::uint8_t byte)
    {
        const std::uint32_t h = head_.load(std::memory_order_relaxed);  // mine
        const std::uint32_t t = load_other(tail_);                      // theirs
        acquire();                        // their tail, then my write

        if (static_cast<std::uint32_t>(h - t) >= ring_size) {
            drops_++;                     // policy, counted and not hidden
            return false;
        }

        buf_[h & ring_mask] = byte;       // the data
        release();                        // data visible before the index
        store_mine(head_, h + 1);         // publish
        return true;
    }

    // Consumer context only. Mirror of the above: the consumer may read a head
    // older than the truth, believe the buffer emptier than it is, and return
    // early. Also conservative, and also incapable of reading a slot that was
    // never written.
    std::optional<std::uint8_t> get()
    {
        const std::uint32_t t = tail_.load(std::memory_order_relaxed);  // mine
        const std::uint32_t h = load_other(head_);                      // theirs
        acquire();                        // their head, then my read

        if (h == t) {
            return std::nullopt;
        }

        const std::uint8_t byte = buf_[t & ring_mask];
        release();                        // read done before the release
        store_mine(tail_, t + 1);
        return byte;
    }

    // Correct across the wrap of the counters, because the subtraction is on
    // unsigned values. Read this from the consumer: from the producer it is
    // conservative in one direction only, and code that treats it as exact will
    // eventually act on a stale number.
    std::uint32_t used() const
    {
        return static_cast<std::uint32_t>(head_.load(std::memory_order_relaxed)
                                          - tail_.load(std::memory_order_relaxed));
    }

    std::uint32_t drops() const { return drops_; }

    // Exposed for the parity comparison, which checks the free-running counters
    // across their own wrap rather than only the occupancy derived from them.
    std::uint32_t head() const { return head_.load(std::memory_order_relaxed); }
    std::uint32_t tail() const { return tail_.load(std::memory_order_relaxed); }

    static constexpr const char* barrier_name() { return name(B); }

private:
    // The four modes, as four pairs of primitives, exactly as barrier.h
    // collapses them in the C. Written as member functions on a constant
    // template parameter rather than as macros, so each one is a compile-time
    // choice the compiler sees through.
    static std::uint32_t load_other(const std::atomic<std::uint32_t>& v)
    {
        if constexpr (B == Barrier::AcqRel) {
            return v.load(std::memory_order_acquire);
        } else {
            return v.load(std::memory_order_relaxed);
        }
    }

    static void store_mine(std::atomic<std::uint32_t>& v, std::uint32_t value)
    {
        if constexpr (B == Barrier::AcqRel) {
            v.store(value, std::memory_order_release);
        } else {
            v.store(value, std::memory_order_relaxed);
        }
    }

    static void acquire()
    {
        if constexpr (B == Barrier::Compiler) {
            std::atomic_signal_fence(std::memory_order_acquire);
        } else if constexpr (B == Barrier::Dmb) {
            std::atomic_thread_fence(std::memory_order_acquire);
        }
    }

    static void release()
    {
        if constexpr (B == Barrier::Compiler) {
            std::atomic_signal_fence(std::memory_order_release);
        } else if constexpr (B == Barrier::Dmb) {
            std::atomic_thread_fence(std::memory_order_release);
        }
    }

    std::array<std::uint8_t, ring_size> buf_{};
    std::atomic<std::uint32_t> head_{0};   // written by the producer only
    std::atomic<std::uint32_t> tail_{0};   // written by the consumer only
    std::uint32_t drops_{0};               // producer only
};

}  // namespace p02

#endif  // P02_RING_HPP
