// bbport: the cost of CPU write-fault tracking (write faults on pages the buffer and texture caches
// protect, Rasterizer::OnWriteFault). Three switches, each on by default:
//   BB_FAULT_PREPARE=0     no range announcement before the HLE bulk writers (memcpy/memset/memmove):
//                          the writes fault page by page again.
//   BB_FAULT_DRAIN=always  a write fault on the GPU command thread always drains the draw pipe (the
//                          old behaviour); else only when a queued packet may read the pages.
//   BB_FAULT_SPINLOCK=1    the region/page locks spin without sleeping again (the old behaviour).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace BbFault {

inline bool Prepare() {
    static const bool on = [] {
        const char* env = std::getenv("BB_FAULT_PREPARE");
        return !(env && env[0] == '0');
    }();
    return on;
}
inline bool DrainAlways() {
    static const bool always = [] {
        const char* env = std::getenv("BB_FAULT_DRAIN");
        return env && std::strcmp(env, "always") == 0;
    }();
    return always;
}
inline bool SpinOnly() {
    static const bool spin = [] {
        const char* env = std::getenv("BB_FAULT_SPINLOCK");
        return env && env[0] == '1';
    }();
    return spin;
}

/// Logs the switches once (called when the page manager starts).
inline void LogSwitches() {
    static bool logged = false;
    if (logged) {
        return;
    }
    logged = true;
    std::printf("Write faults: range announce before HLE writes %s (BB_FAULT_PREPARE=0 off), draw pipe "
                "drain %s (BB_FAULT_DRAIN=always), region locks %s (BB_FAULT_SPINLOCK=1 spin)\n",
                Prepare() ? "on" : "off", DrainAlways() ? "always" : "only when queued draws may "
                "read the page",
                SpinOnly() ? "spinning" : "spin then sleep");
}

/// Counters (BB_FRAME_STATS output, driver.cpp).
/// A: announced ranges (calls that found protected pages), the protected pages in them (faults
/// avoided), ns spent announcing.
inline std::atomic<std::uint64_t> prepare_ranges{0}, prepare_pages{0}, prepare_ns{0};
/// B: write faults on the GPU command thread that drained the draw pipe, and that did not.
inline std::atomic<std::uint64_t> drains_done{0}, drains_skipped{0};
/// C: lock acquisitions that did not succeed at once, and their wait (ns), in all, and on the
/// write fault / announcement path.
inline std::atomic<std::uint64_t> lock_waits{0}, lock_wait_ns{0}, fault_lock_waits{0},
    fault_lock_wait_ns{0}, lock_sleeps{0};
/// Set while this thread handles a write fault or announces a write.
inline thread_local bool in_fault_path = false;
struct FaultScope {
    bool previous = in_fault_path;
    FaultScope() {
        in_fault_path = true;
    }
    ~FaultScope() {
        in_fault_path = previous;
    }
};

} // namespace BbFault

namespace Common {

inline void CpuPause() {
#if defined(__aarch64__)
    asm volatile("yield");
#elif defined(__x86_64__)
    asm volatile("pause");
#endif
}

/// A lock that spins briefly and then sleeps in the kernel (futex): the buffer cache's region locks
/// and the page manager's locks were spin locks, and a thread waiting for the recording thread's
/// upload (or for a preempted holder) burned a core in __aarch64_swp1_acq. Async-signal-safe:
/// write faults take it from the faulting thread's signal handler. BB_FAULT_SPINLOCK=1 spins
/// without end, as the spin lock did.
class SleepMutex {
public:
    SleepMutex() = default;
    SleepMutex(const SleepMutex&) = delete;
    SleepMutex& operator=(const SleepMutex&) = delete;

    void lock() {
        std::uint32_t expected = 0;
        if (state.compare_exchange_strong(expected, 1, std::memory_order_acquire,
                                          std::memory_order_relaxed)) [[likely]] {
            return;
        }
        LockSlow();
    }
    [[nodiscard]] bool try_lock() {
        std::uint32_t expected = 0;
        return state.compare_exchange_strong(expected, 1, std::memory_order_acquire,
                                             std::memory_order_relaxed);
    }
    void unlock() {
        if (state.exchange(0, std::memory_order_release) == 2) {
            syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&state), FUTEX_WAKE_PRIVATE, 1,
                    nullptr, nullptr, 0);
        }
    }

private:
    bool TrySpin(std::uint32_t iterations) {
        for (std::uint32_t i = 0; i < iterations; ++i) {
            std::uint32_t expected = 0;
            if (state.load(std::memory_order_relaxed) == 0 &&
                state.compare_exchange_weak(expected, 1, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                return true;
            }
            CpuPause();
        }
        return false;
    }
    void NoteWait(std::chrono::steady_clock::time_point start) {
        const auto ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start)
                .count());
        BbFault::lock_waits.fetch_add(1, std::memory_order_relaxed);
        BbFault::lock_wait_ns.fetch_add(ns, std::memory_order_relaxed);
        if (BbFault::in_fault_path) {
            BbFault::fault_lock_waits.fetch_add(1, std::memory_order_relaxed);
            BbFault::fault_lock_wait_ns.fetch_add(ns, std::memory_order_relaxed);
        }
    }
    [[gnu::noinline]] void LockSlow() {
        const auto start = std::chrono::steady_clock::now();
        if (BbFault::SpinOnly()) {
            while (!TrySpin(1u << 16)) {
            }
            NoteWait(start);
            return;
        }
        // Region locks guard short sections: most waits end within a few hundred iterations.
        if (TrySpin(256)) {
            NoteWait(start);
            return;
        }
        // Drepper's futex mutex: 0 free, 1 locked, 2 locked with sleepers.
        for (;;) {
            if (state.exchange(2, std::memory_order_acquire) == 0) {
                break;
            }
            BbFault::lock_sleeps.fetch_add(1, std::memory_order_relaxed);
            syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&state), FUTEX_WAIT_PRIVATE, 2,
                    nullptr, nullptr, 0);
        }
        NoteWait(start);
    }

    std::atomic<std::uint32_t> state{0};
};
static_assert(sizeof(SleepMutex) == 4);

} // namespace Common
