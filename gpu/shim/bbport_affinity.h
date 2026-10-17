// SPDX-License-Identifier: GPL-2.0-or-later
// bbport BB_PIN_THREADS=1: the game's main thread on the fastest cores, the heavy helper threads on
// the middle ones (bbport_affinity.cpp).
#pragma once

namespace BbAffinity {
/// The calling thread runs the game's main loop (src/probe.c, after the module initializers).
void MainThread();
/// The calling thread was named `name` (shadPS4 threads, and guest threads through the runtime).
void ThreadNamed(const char* name);
/// Once a frame: puts back the affinities the system changed (at most every 2 s).
void Maintain();
} // namespace BbAffinity
