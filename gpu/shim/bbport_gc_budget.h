// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the texture collector's memory budget, live (BB_GC_BUDGET_MB is its start). On a phone the
// GPU's memory is the system's RAM, shared with the game's direct memory and with Android: when the
// system runs short (MemAvailable low, zram swap filling) the budget steps down, and the collector
// frees idle textures sooner; it steps back up, slowly, when memory recovers (never above the
// start). BB_GC_ADAPT=0 keeps the start value.
//
// The budget is an atomic read by whichever thread runs the collector (GarbageCollectImages) and
// written by the watcher thread (bb:MemWatch, every 2 s), nothing else is shared.

#pragma once

#include <cstdint>

namespace BbGcBudget {
/// The collector's budget in bytes now; 0 when BB_GC_BUDGET_MB is not set (the driver's own
/// budget is used then). Starts the watcher thread at the first call.
std::uint64_t Bytes();
/// The system is short of memory: the collector frees textures idle for a few seconds, not 20.
bool Short();
} // namespace BbGcBudget
