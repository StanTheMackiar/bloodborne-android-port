package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;

/** The GPU memory budgets for this phone's RAM (no Android classes, so it is tested on a desktop JDK).
 *
 *  On a phone the GPU's memory is system RAM, shared with the game and Android, and Turnip reports a heap of all
 *  of it (8.3 GB on a 12 GB phone). Two budgets follow from that, both tuned on 16 GB phones:
 *  - BB_GC_BUDGET_MB (run-bionic.sh: 4096): the texture collector starts freeing images at 70% of it;
 *  - the VRAM budget the renderer takes from the driver: copies of the game's memory go into Vulkan buffers until
 *    90% of it, and on Android they are never returned (that needs the in-place memory model). BB_VRAM_LIMIT_MB=N
 *    takes it as at most N MiB.
 *  On a 12 GB phone (MemTotal 10.8 GiB), without restarting it between sessions, the copies grew to 1.3 GB in
 *  Central Yharnam and the phone swapped: 2-5 FPS after ~4 minutes. With both at 2048 the game stayed playable
 *  (~25-28 FPS) for about 8 minutes (issue #8). The app passes them by MemTotal; bionic-local.sh, sourced after,
 *  still overrides them, and bbport.ini gpu_memory_budget_mb=N sets both by hand. */
final class MemoryBudget {
    /** The texture collector's budget, and the VRAM budget taken from the driver (0: the driver's own), in MiB. */
    final long textureMb, vramMb;
    /** MemTotal in KiB (0 when unknown) and why these values: one log line. */
    final long memTotalKb;
    final String reason;

    private MemoryBudget(long textureMb, long vramMb, long memTotalKb, String reason) {
        this.textureMb = textureMb; this.vramMb = vramMb; this.memTotalKb = memTotalKb; this.reason = reason;
    }

    static final long GIB_KB = 1L << 20;

    /** The values for a phone with `memTotalKb` of MemTotal. Phones report less than their RAM (the modem and the
     *  camera keep some): 16 GB phones ~15 GiB, 12 GB ~10.8 GiB, 8 GB ~7.3 GiB. 16 GB phones and an unknown MemTotal
     *  keep run-bionic.sh's budget and the driver's VRAM budget, as before. */
    static MemoryBudget forMemTotal(long memTotalKb) {
        String ram = memTotalKb > 0 ? String.format(Locale.ROOT, "%.1f GB RAM", memTotalKb / (double)GIB_KB) : "RAM unknown";
        if (memTotalKb <= 0 || memTotalKb >= 14 * GIB_KB) return new MemoryBudget(4096, 0, memTotalKb, ram + ": the default budget");
        if (memTotalKb >= 10 * GIB_KB) return new MemoryBudget(2048, 2048, memTotalKb, ram + ": a 12 GB phone");
        if (memTotalKb >= 6 * GIB_KB) return new MemoryBudget(1536, 1536, memTotalKb, ram + ": an 8 GB phone");
        return new MemoryBudget(1024, 1024, memTotalKb, ram + ": a phone with less than 8 GB");
    }

    /** The values for `memTotalKb`, with both budgets set by hand (setting: bbport.ini gpu_memory_budget_mb, may be
     *  null) when it is a number from 512 to 16384. */
    static MemoryBudget choose(long memTotalKb, String setting) {
        MemoryBudget auto = forMemTotal(memTotalKb);
        if (setting != null) {
            try {
                long mb = Long.parseLong(setting.trim());
                if (mb >= 512 && mb <= 16384)
                    return new MemoryBudget(mb, mb, memTotalKb, auto.reason.replaceFirst(":.*", "") + ": set in bbport.ini");
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

    /** The environment run-bionic.sh reads (BB_VRAM_LIMIT_MB only when there is a limit). */
    String[] environment() {
        return vramMb > 0 ? new String[]{"BB_GC_BUDGET_MB=" + textureMb, "BB_VRAM_LIMIT_MB=" + vramMb}
                          : new String[]{"BB_GC_BUDGET_MB=" + textureMb};
    }

    /** One line for the game log. */
    String logLine() {
        return "Memory: " + reason + " -> textures " + textureMb + " MB, GPU memory "
            + (vramMb > 0 ? "taken as " + vramMb + " MB" : "as the driver reports");
    }
}
