// SPDX-License-Identifier: GPL-2.0-or-later
// bbport BB_PIN_THREADS=1: thread placement on phones with big.LITTLE clusters.
//
// On a Poco F7 Pro (Snapdragon 8 Gen 3: cpu7 Cortex-X4 3.3 GHz, cpu2-6 Cortex-A720 2.96-3.15 GHz,
// cpu0-1 Cortex-A520) the game's main thread (FEX running the game's x86 main loop) moved between
// cpu2, 4, 5 and 7 every few seconds. It is what limits the frame rate (the GPU threads were idle
// half of each frame), and the X4 does more per cycle than the A720s at the same clock. Here:
//   - the main thread: only the fastest cores (the highest cpuinfo_max_freq);
//   - the port's busy helpers (GPU command processor, draw recording, copies) and the game's
//     render and audio mixer threads: the middle cores (above the slowest, below the fastest),
//     so they leave the fastest core to the main thread;
//   - every other thread: unchanged.
// The system may change affinities (HyperOS resets them, GitHub issue #3): Maintain() puts them
// back, at most every 2 s, and counts how often that happened.
#include "bbport_affinity.h"

#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace BbAffinity {
namespace {

struct Layout {
    cpu_set_t fast{}, middle{};
    bool usable = false;
};

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BB_PIN_THREADS");
        return v && v[0] == '1';
    }();
    return on;
}

unsigned long MaxFreq(int cpu) {
    char path[96];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    unsigned long khz = 0;
    if (FILE* f = std::fopen(path, "r")) {
        if (std::fscanf(f, "%lu", &khz) != 1) {
            khz = 0;
        }
        std::fclose(f);
    }
    return khz;
}

std::string List(const cpu_set_t& set) {
    std::string out;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set)) {
            out += (out.empty() ? "" : ",") + std::to_string(cpu);
        }
    }
    return out;
}

const Layout& Get() {
    static const Layout layout = [] {
        Layout l;
        const int count = int(sysconf(_SC_NPROCESSORS_CONF));
        std::vector<unsigned long> freq(std::max(count, 0));
        unsigned long fastest = 0, slowest = ~0ul;
        for (int cpu = 0; cpu < count; ++cpu) {
            freq[cpu] = MaxFreq(cpu);
            if (freq[cpu]) {
                fastest = std::max(fastest, freq[cpu]);
                slowest = std::min(slowest, freq[cpu]);
            }
        }
        if (fastest == 0 || fastest == slowest) {
            std::printf("Affinity: one cluster or no frequencies: threads not pinned\n");
            return l;
        }
        for (int cpu = 0; cpu < count; ++cpu) {
            if (freq[cpu] == fastest) {
                CPU_SET(cpu, &l.fast);
            } else if (freq[cpu] > slowest) {
                CPU_SET(cpu, &l.middle);
            }
        }
        if (CPU_COUNT(&l.middle) == 0) {
            l.middle = l.fast; // two clusters: the helpers share the fast ones
        }
        l.usable = true;
        std::printf("Affinity: main thread on cpus %s, helpers on cpus %s (BB_PIN_THREADS=1)\n",
                    List(l.fast).c_str(), List(l.middle).c_str());
        return l;
    }();
    return layout;
}

struct Pinned {
    pid_t tid;
    bool fast;
    char name[16];
};

std::mutex lock;
std::vector<Pinned> pinned;
unsigned resets = 0;

void Pin(bool fast, const char* name) {
    if (!Enabled()) {
        return;
    }
    const Layout& l = Get();
    if (!l.usable) {
        return;
    }
    const pid_t tid = pid_t(syscall(SYS_gettid));
    const cpu_set_t& set = fast ? l.fast : l.middle;
    if (sched_setaffinity(tid, sizeof(set), &set) != 0) {
        std::printf("Affinity: %s (tid %d) could not be pinned to cpus %s\n", name, int(tid),
                    List(set).c_str());
        return;
    }
    std::scoped_lock lk{lock};
    Pinned p{tid, fast, {}};
    std::strncpy(p.name, name, sizeof(p.name) - 1);
    pinned.push_back(p);
}

bool Helper(const char* name) {
    static constexpr const char* prefixes[] = {
        "shadPS4:GpuComm", "bb:DrawRec", "bb:VkRecorder", "bb:DrawPrep", "bb:DrawScan",
        "bb:Copy",         "GXRender",   "FMOD mixer",
    };
    for (const char* prefix : prefixes) {
        if (!std::strncmp(name, prefix, std::strlen(prefix))) {
            return true;
        }
    }
    return false;
}
} // namespace

void MainThread() {
    Pin(true, "main thread");
}

void ThreadNamed(const char* name) {
    if (name && Enabled() && Helper(name)) {
        Pin(false, name);
    }
}

void Maintain() {
    if (!Enabled()) {
        return;
    }
    static auto next = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now < next) {
        return;
    }
    next = now + std::chrono::seconds(2);
    const Layout& l = Get();
    std::scoped_lock lk{lock};
    unsigned put_back = 0;
    for (const Pinned& p : pinned) {
        const cpu_set_t& want = p.fast ? l.fast : l.middle;
        cpu_set_t have;
        CPU_ZERO(&have);
        if (sched_getaffinity(p.tid, sizeof(have), &have) != 0) {
            continue; // the thread ended
        }
        if (!CPU_EQUAL(&have, &want) && sched_setaffinity(p.tid, sizeof(want), &want) == 0) {
            ++put_back;
        }
    }
    if (put_back) {
        resets += put_back;
        std::printf("Affinity: %u threads had other cpus set by the system, put back (%u so far)\n",
                    put_back, resets);
    }
}
} // namespace BbAffinity
