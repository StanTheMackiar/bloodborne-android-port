// bbport: host threads that may call guest code (AvPlayer allocator callbacks).
// Each thread gets a guest TCB (GS base, TLS) from the C runtime before running.
#pragma once
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include "common/types.h"

extern "C" void runtime_thread_attach_host(const char* name);

namespace Libraries::Kernel {
class Thread {
public:
    Thread() = default;
    ~Thread() { Stop(); }
    void Run(std::function<void(std::stop_token)>&& func) {
        std::jthread old;
        {
            std::lock_guard lock{mutex};
            old = std::move(thread);
            thread = std::jthread([func = std::move(func)](std::stop_token stop) {
                runtime_thread_attach_host("bb:hle");
                func(stop);
            });
        }
        // A thread still running from before ends as Stop() would end it (outside the lock).
        if (old.joinable()) {
            old.request_stop();
            if (old.get_id() == std::this_thread::get_id()) old.detach();
            else old.join();
        }
    }
    // A thread may stop its own Thread object (AvPlayer does); it detaches instead of joining.
    // bbport: AvPlayer's controller thread stops the player when the movie ends while the game
    // stops it too (sceAvPlayerStop, entering character creation): one thread joined while the
    // other detached, and Bionic's pthread_detach of a thread being joined fails (EINVAL:
    // "thread::detach failed", the game ended). The handle is taken under a lock: whoever takes it
    // joins outside the lock; the thread itself detaches only if no one took it, else it just ends.
    void Join() { Finish(false); }
    bool Joinable() const {
        std::lock_guard lock{mutex};
        return thread.joinable();
    }
    void Stop() { Finish(true); }

private:
    void Finish(bool stop) {
        std::jthread taken;
        {
            std::lock_guard lock{mutex};
            if (!thread.joinable()) return;
            if (stop) thread.request_stop();
            if (thread.get_id() == std::this_thread::get_id()) {
                thread.detach();
                return;
            }
            taken = std::move(thread);
        }
        taken.join();
    }

    mutable std::mutex mutex;
    std::jthread thread;
};
} // namespace Libraries::Kernel
