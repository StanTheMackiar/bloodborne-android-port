package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.function.Consumer;

/** Hides movies from the game: the folder it runs from (BB_APP0_DIR: the game folder, or the mods overlay over it)
 *  is seen through a small tree of links that leaves them out, so the game cannot open them and goes on as if they
 *  had ended. The player's files are never changed (no Android classes, so it is tested on a desktop JDK).
 *
 *  The opening movie, played before character creation, crashes the game on some phones when it ends or is
 *  skipped: the movie player's threads join and detach each other (issue #17). Without the file the game goes
 *  straight to character creation. Only the folders on the way to a hidden file are made; everything else is one
 *  link to the folder it came from. */
final class MovieSkip {
    /** The movie before character creation (game paths, posix, from the game folder). */
    static final List<String> OPENING = Collections.unmodifiableList(Arrays.asList("dvdroot_ps4/movie/sprj_opening.mp4"));

    private MovieSkip() {}

    /** A new folder `out` of links over `app0` without the files `hidden` (matched ignoring case), and returns it;
     *  `app0` itself when none of them is there, or when the links cannot be made (nothing is left behind then).
     *  `out` is deleted first. `log` gets the "Movies: ..." lines. */
    static File apply(File app0, File out, List<String> hidden, Consumer<String> log) {
        FsUtil.deleteTree(out);
        try {
            File source = FsUtil.resolveStrict(app0);
            Set<String> files = new HashSet<>(), folders = new HashSet<>();
            for (String path : hidden) {
                if (!find(source, path).isFile()) continue;
                String key = FsUtil.casefold(path);
                files.add(key);
                for (int at = key.indexOf('/'); at >= 0; at = key.indexOf('/', at + 1)) folders.add(key.substring(0, at));
            }
            if (files.isEmpty()) {
                log.accept("Movies: " + String.join(", ", hidden) + " not in the game folder, nothing to hide");
                return app0;
            }
            int[] left = {0};
            mirror(source, out, "", files, folders, left);
            log.accept("Movies: " + left[0] + " hidden from the game (" + String.join(", ", hidden) + ")");
            return out;
        } catch (IOException | RuntimeException e) {
            FsUtil.deleteTree(out);
            log.accept("Movies: could not hide them, starting with them: " + e.getMessage());
            return app0;
        }
    }

    /** `relative` inside `root`, each part matched ignoring case (the game's names are lower case, a dump's may not be). */
    private static File find(File root, String relative) {
        File at = root;
        for (String part : relative.split("/")) at = FsUtil.child(at, part);
        return at;
    }

    /** `target` as a folder: links to the entries of `source`, except the hidden files and the folders on their way,
     *  which are made again the same way. */
    private static void mirror(File source, File target, String prefix, Set<String> files, Set<String> folders,
                               int[] hidden) throws IOException {
        if (!target.mkdirs() && !target.isDirectory()) throw new IOException("Cannot create " + target);
        for (String name : FsUtil.names(source)) {
            String key = FsUtil.casefold(prefix.isEmpty() ? name : prefix + "/" + name);
            File entry = new File(source, name);
            if (files.contains(key)) { ++hidden[0]; continue; }
            if (folders.contains(key) && entry.isDirectory()) mirror(entry, new File(target, name), key, files, folders, hidden);
            else Files.createSymbolicLink(new File(target, name).toPath(), entry.toPath());
        }
    }
}
