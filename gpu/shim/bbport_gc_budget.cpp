// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the texture collector's memory budget, live (bbport_gc_budget.h).
//
// Rules (every 2 s, from /proc/meminfo; the memory pressure stall file /proc/pressure/memory too
// where an app may read it):
//   short:    MemAvailable < 1 GiB, or zram/swap more than 50 % used (or memory stalls: PSI "full"
//             avg10 >= 5): the budget steps down 256 MB, 512 MB when critical (MemAvailable <
//             512 MB, swap over 80 %, PSI full avg10 >= 15).
//   floor:    never below 1024 MB, and never below what the cache holds now / 0.8 (BbStats
//             gc_used_bytes): the budget never drops under the usage so far that recently used
//             textures would be evicted; only what is idle goes (the collector's idle time falls to
//             5 s while short), and the budget follows the usage down as that is freed.
//   relaxed:  MemAvailable > 2 GiB and swap under 40 % used, for 10 s in a row: +128 MB a step,
//             up to the start value, never above it.
//   between:  held.
// Changes are logged at most once every 5 s.
#include "bbport_gc_budget.h"
#include "bbport_toggles.h"

#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace BbGcBudget {
namespace {
using u64 = std::uint64_t;
constexpr u64 MiB = 1ull << 20;
constexpr u64 FloorMiB = 1024;

std::atomic<u64> budget{0};      ///< bytes; 0: not set
std::atomic<bool> is_short{false};
u64 start_bytes = 0;

struct Memory {
    u64 available = 0, swap_total = 0, swap_free = 0; ///< bytes
    bool have = false;
    double psi_full = -1.0; ///< PSI memory "full" avg10, -1: not readable
};

Memory Read() {
    Memory m;
    if (FILE* f = std::fopen("/proc/meminfo", "r")) {
        char line[128];
        int found = 0;
        while (std::fgets(line, sizeof(line), f) && found < 3) {
            unsigned long long kb;
            if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1) {
                m.available = kb * 1024, ++found;
            } else if (std::sscanf(line, "SwapTotal: %llu kB", &kb) == 1) {
                m.swap_total = kb * 1024, ++found;
            } else if (std::sscanf(line, "SwapFree: %llu kB", &kb) == 1) {
                m.swap_free = kb * 1024, ++found;
            }
        }
        std::fclose(f);
        m.have = found == 3;
    }
    if (FILE* f = std::fopen("/proc/pressure/memory", "r")) {
        char line[160];
        while (std::fgets(line, sizeof(line), f)) {
            double avg10;
            if (std::sscanf(line, "full avg10=%lf", &avg10) == 1) {
                m.psi_full = avg10;
            }
        }
        std::fclose(f);
    }
    return m;
}

void Watch() {
    pthread_setname_np(pthread_self(), "bb:MemWatch");
    using clock = std::chrono::steady_clock;
    u64 current = start_bytes;
    int relaxed_steps = 0;
    u64 logged = current;
    auto last_log = clock::now() - std::chrono::seconds(10);
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const Memory m = Read();
        if (!m.have) {
            continue;
        }
        const double swap_used =
            m.swap_total ? double(m.swap_total - std::min(m.swap_free, m.swap_total)) /
                               double(m.swap_total) * 100.0
                         : 0.0;
        const bool critical = m.available < 512 * MiB || swap_used > 80.0 || m.psi_full >= 15.0;
        const bool tight = critical || m.available < 1024 * MiB || swap_used > 50.0 ||
                           m.psi_full >= 5.0;
        const bool relaxed = m.available > 2048 * MiB && swap_used < 40.0 &&
                             (m.psi_full < 0.0 || m.psi_full < 1.0);
        u64 next = current;
        if (tight) {
            relaxed_steps = 0;
            const u64 used = BbStats::gc_used_bytes.load(std::memory_order_relaxed);
            // Not below the floor, nor below the usage / 0.8 (nor above where it is).
            const u64 floor_bytes = std::min(current, std::max(FloorMiB * MiB, used / 4 * 5));
            const u64 step = (critical ? 512 : 256) * MiB;
            next = current > floor_bytes + step ? current - step : floor_bytes;
        } else if (relaxed) {
            if (++relaxed_steps >= 5 && current < start_bytes) {
                next = std::min(start_bytes, current + 128 * MiB);
            }
        } else {
            relaxed_steps = 0;
        }
        if (next != current) {
            current = next;
            budget.store(current, std::memory_order_relaxed);
        }
        is_short.store(tight || current < start_bytes, std::memory_order_relaxed);
        const auto now = clock::now();
        if (current != logged && now - last_log >= std::chrono::seconds(5)) {
            std::printf("Texture cache: memory budget %llu -> %llu MB (MemAvailable %llu MB, swap "
                        "%.0f%% of %llu MB used%s)\n",
                        (unsigned long long)(logged >> 20), (unsigned long long)(current >> 20),
                        (unsigned long long)(m.available >> 20), swap_used,
                        (unsigned long long)(m.swap_total >> 20),
                        m.psi_full >= 0.0 ? ", memory stalls" : "");
            std::fflush(stdout);
            logged = current;
            last_log = now;
        }
    }
}
} // namespace

u64 Bytes() {
    static const bool started = [] {
        const char* env = std::getenv("BB_GC_BUDGET_MB");
        const u64 mb = env && *env ? std::strtoull(env, nullptr, 10) : 0;
        if (mb == 0) {
            return false;
        }
        start_bytes = mb * MiB;
        budget.store(start_bytes, std::memory_order_relaxed);
        const char* adapt = std::getenv("BB_GC_ADAPT");
        if (!(adapt && adapt[0] == '0') && start_bytes > FloorMiB * MiB) {
            std::thread(Watch).detach();
        }
        return true;
    }();
    return started ? budget.load(std::memory_order_relaxed) : 0;
}

bool Short() {
    return is_short.load(std::memory_order_relaxed);
}
} // namespace BbGcBudget
