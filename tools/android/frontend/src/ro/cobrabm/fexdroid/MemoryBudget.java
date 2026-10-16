package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;

/** The GPU memory budget for this phone's RAM (no Android classes, so it is tested on a desktop JDK).
 *
 *  On a phone the GPU's memory is system RAM, shared with the game and Android. run-bionic.sh's budget
 *  (BB_GC_BUDGET_MB=4096: the texture collector starts freeing at 70%, presses at 85%, is critical at 95%;
 *  BB_VRAM_IDLE_SECONDS=20: idle copies of game memory go back after that long) is tuned on 16 GB phones.
 *  On a 12 GB phone (11.4 GB MemTotal) it left the phone swapping: 2.8 GB of Vulkan memory in character
 *  creation, ~100 MB/s of swap traffic and 10 FPS (issue #8). The app passes these instead, by MemTotal;
 *  bionic-local.sh, sourced after, still overrides them, and bbport.ini gpu_memory_budget_mb=N sets the
 *  budget by hand. */
final class MemoryBudget {
    /** The budget in MiB and how long an idle copy is kept, in seconds. */
    final long budgetMb;
    final int idleSeconds;
    /** MemTotal in KiB (0 when unknown) and why these values: one log line. */
    final long memTotalKb;
    final String reason;

    private MemoryBudget(long budgetMb, int idleSeconds, long memTotalKb, String reason) {
        this.budgetMb = budgetMb; this.idleSeconds = idleSeconds; this.memTotalKb = memTotalKb; this.reason = reason;
    }

    static final long GIB_KB = 1L << 20;

    /** The values for a phone with `memTotalKb` of MemTotal. Phones report less than their RAM (the modem and
     *  the camera keep some): 16 GB phones ~15 GB, 12 GB ~11.4 GB, 8 GB ~7.5 GB. Unknown: run-bionic.sh's own. */
    static MemoryBudget forMemTotal(long memTotalKb) {
        String ram = memTotalKb > 0 ? String.format(Locale.ROOT, "%.1f GB RAM", memTotalKb / (double)GIB_KB) : "RAM unknown";
        if (memTotalKb <= 0) return new MemoryBudget(4096, 20, 0, ram + ": the default budget");
        if (memTotalKb >= 14 * GIB_KB) return new MemoryBudget(4096, 20, memTotalKb, ram + ": the default budget");
        if (memTotalKb >= 10 * GIB_KB) return new MemoryBudget(2048, 10, memTotalKb, ram + ": a 12 GB phone");
        if (memTotalKb >= 6 * GIB_KB) return new MemoryBudget(1536, 10, memTotalKb, ram + ": an 8 GB phone");
        return new MemoryBudget(1024, 10, memTotalKb, ram + ": a phone with less than 8 GB");
    }

    /** The values for `memTotalKb`, with the budget set by hand (setting: bbport.ini gpu_memory_budget_mb, may be
     *  null) when it is a number from 512 to 16384. */
    static MemoryBudget choose(long memTotalKb, String setting) {
        MemoryBudget auto = forMemTotal(memTotalKb);
        if (setting != null) {
            try {
                long mb = Long.parseLong(setting.trim());
                if (mb >= 512 && mb <= 16384)
                    return new MemoryBudget(mb, auto.idleSeconds, memTotalKb, auto.reason.replaceFirst(":.*", "") + ": set in bbport.ini");
            } catch (NumberFormatException ignored) {}
        }
        return auto;
    }

    /** MemTotal from a /proc/meminfo text, in KiB; 0 when it is not there. */
    static long memTotalKb(String meminfo) {
        for (String line : meminfo.split("\n")) {
            if (!line.startsWith("MemTotal:")) continue;
            String[] parts = line.substring(9).trim().split("\\s+");
            try { return Long.parseLong(parts[0]); } catch (NumberFormatException | ArrayIndexOutOfBoundsException e) { return 0; }
        }
        return 0;
    }

    /** MemTotal of this phone, in KiB; 0 when /proc/meminfo cannot be read. */
    static long memTotalKb(File meminfo) {
        try { return memTotalKb(new String(Files.readAllBytes(meminfo.toPath()), StandardCharsets.US_ASCII)); }
        catch (IOException e) { return 0; }
    }

    /** The environment run-bionic.sh reads. */
    String[] environment() {
        return new String[]{"BB_GC_BUDGET_MB=" + budgetMb, "BB_VRAM_IDLE_SECONDS=" + idleSeconds};
    }

    /** One line for the game log. */
    String logLine() {
        return "Memory: " + reason + " -> GPU memory budget " + budgetMb + " MB, idle copies freed after " + idleSeconds + " s";
    }
}
