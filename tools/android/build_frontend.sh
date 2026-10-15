#!/usr/bin/env bash
# Build the Bloodborne Android app (Java UI + libbbpresent.so, the frame path of the Bionic game process).
# With BB_RUNTIME_ARCHIVE (build_release_runtime.py) the runtime archive goes inside the APK as an asset.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
sdk=${BB_ANDROID_SDK:-"$PWD/.local-deps/android/sdk"}
build_tools="$sdk/build-tools/35.0.0"
android_jar="$sdk/platforms/android-35/android.jar"
local_jdk="$PWD/.local-deps/android/jdk/usr/lib/jvm/java-27-openjdk"
if [[ -x $local_jdk/bin/javac ]]; then
    export JAVA_HOME="$local_jdk" PATH="$local_jdk/bin:$PATH"
fi
out="$PWD/out/thor/frontend"
rm -rf "$out/classes" "$out/dex" "$out/res" "$out/assets" "$out/lib"
mkdir -p "$out/classes" "$out/dex" "$out/res/drawable" "$out/assets"
cp -r tools/android/frontend/res/. "$out/res/"
if [[ -n ${BB_RUNTIME_ARCHIVE:-} ]]; then
    cp "$BB_RUNTIME_ARCHIVE" "$out/assets/runtime.payload"
    cp "${BB_RUNTIME_ARCHIVE%.tar.gz}.sha256" "$out/assets/runtime.sha256"
fi
# Use the user's own Bloodborne icon locally; game art is not committed to this repository.
python3 - "$out" <<'PY'
import os, sys
from pathlib import Path
from PIL import Image
out = Path(sys.argv[1])
icon = Path('game/CUSA03173/sce_sys/icon0.dds')
if os.environ.get('BB_RUNTIME_ARCHIVE') and not icon.exists():
    raise SystemExit('Release build needs the user-owned game icon at game/CUSA03173/sce_sys/icon0.dds')
if icon.exists():
    (out/'res/drawable/icon.xml').unlink(missing_ok=True)
    Image.open(icon).save(out/'res/drawable/icon.png')
PY
cp tools/android/frontend/THIRD_PARTY_LICENSES.txt "$out/assets/"
# The Mods menu's libraries (ModArchive.java: Apache Commons Compress and xz, for .zip and .7z mods).
mod_libs=$PWD/.local-deps/android/mod-libs
[[ -f $mod_libs/jars.txt ]] || bash tools/android/fetch_mod_libs.sh
mapfile -t lib_jars < <(sed "s|^|$mod_libs/jars/|" "$mod_libs/jars.txt")
mkdir -p "$out/lib/arm64-v8a"
# libbbpresent.so: the Bionic game's frame path (AHardwareBuffers -> ASurfaceControl), built with the
# NDK at minSdk 28 (the API 29 calls are looked up at run time). NEEDED: libandroid, liblog,
# libnativewindow and libc/libm/libdl only.
ndk=${BB_NDK:-$PWD/.local-deps/android/sdk/ndk/29.0.14206865}
[[ -x $ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang ]] \
    || { echo "No NDK: $ndk (tools/android/build_bionic_deps.sh)" >&2; exit 1; }
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang" -fuse-ld=lld -std=c11 -O2 -g \
    -Wall -Wextra -Werror -fPIC -shared -Wl,-soname,libbbpresent.so -Wl,-z,max-page-size=16384 \
    -Itools/android/present tools/android/frontend/jni/bbpresent/bbpresent.c \
    -landroid -llog -lnativewindow -o "$out/lib/arm64-v8a/libbbpresent.so"
mapfile -t sources < <(rg --files tools/android/frontend/src -g '*.java')
javac --release 8 -classpath "$android_jar:$(IFS=:; echo "${lib_jars[*]}")" -d "$out/classes" "${sources[@]}"
mapfile -t classes < <(rg --files "$out/classes" -g '*.class')
"$build_tools/d8" --release --min-api 28 --lib "$android_jar" --output "$out/dex" "${classes[@]}" "${lib_jars[@]}"
"$build_tools/aapt" package -f -M tools/android/frontend/AndroidManifest.xml \
    -S "$out/res" -A "$out/assets" -I "$android_jar" -0 payload -F "$out/unsigned.apk"
python3 - "$out" <<'PY'
import sys, zipfile
from pathlib import Path
out = Path(sys.argv[1])
with zipfile.ZipFile(out/'unsigned.apk','a',compression=zipfile.ZIP_DEFLATED) as z:
    for p in sorted((out/'dex').glob('*.dex')): z.write(p,p.name)
    for p in sorted((out/'lib').rglob('*.so')): z.write(p,str(p.relative_to(out)))
PY
"$build_tools/zipalign" -f 4 "$out/unsigned.apk" "$out/aligned.apk"
key="$PWD/.local-deps/android/bloodborne-thor.keystore"
if [[ ! -f $key ]]; then
    keytool -genkeypair -keystore "$key" -storepass android -keypass android \
        -alias thor -dname 'CN=Bloodborne Thor local build' -keyalg RSA -keysize 2048 -validity 10000
fi
"$build_tools/apksigner" sign --ks "$key" --ks-pass pass:android --key-pass pass:android \
    --out "$out/Bloodborne-Thor.apk" "$out/aligned.apk"
"$build_tools/apksigner" verify --verbose "$out/Bloodborne-Thor.apk"
echo "Built $out/Bloodborne-Thor.apk"
