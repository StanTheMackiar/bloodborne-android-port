#!/usr/bin/env bash
# Verifies the Bionic outputs: NEEDED entries are Bionic/NDK/our own libraries only, the executable
# is PIE with /system/bin/linker64, and every undefined symbol of every output is provided by the
# libraries it is linked against, the executable (runtime_* symbols libbbgpu takes from it), or
# Bionic itself. No libvulkan anywhere: the driver is dlopen'ed (gpu/shim/vk_driver_loader.cpp).
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
out=out/bionic
ndk=${BB_NDK:-$PWD/.local-deps/android/sdk/ndk/29.0.14206865}
llvm=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
api=${BB_ANDROID_API:-30}
sysroot_libs=$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/$api
fail=0
allowed='^(libc\.so|libm\.so|libdl\.so|liblog\.so|libandroid\.so|libc\+\+_shared\.so|libbbgpu\.so|libbbcpu\.so|libSDL3\.so)$'
defined() { "$llvm/llvm-nm" -D --defined-only "$@" 2>/dev/null | awk 'NF>=3{print $3}' | sed 's/@.*//'; }
provided=$(mktemp); trap 'rm -f "$provided"' EXIT
{ defined "$out/bin/bb-probe" "$out/lib/libbbgpu.so" "$out/lib/libbbcpu.so" "$out/lib/libSDL3.so" "$out/lib/libc++_shared.so"
  for lib in libc.so libm.so libdl.so liblog.so libandroid.so; do defined "$sysroot_libs/$lib"; done; } | sort -u > "$provided"
for f in "$out/bin/bb-probe" "$out/bin/kgsl-chip-id" "$out/lib/libbbgpu.so" "$out/lib/libbbcpu.so"; do
    echo "== $f"
    "$llvm/llvm-readelf" -d "$f" | grep -E 'NEEDED|SONAME|RUNPATH' | sed 's/^ *0x[0-9a-f]* *//'
    for lib in $("$llvm/llvm-readelf" -d "$f" | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p'); do
        [[ $lib =~ $allowed ]] || { echo "  NOT ALLOWED: $lib"; fail=1; }
        [[ $lib == libvulkan.so ]] && { echo "  libvulkan.so must not be linked"; fail=1; }
    done
    case $f in *bin/*) "$llvm/llvm-readelf" -l "$f" | grep -E 'Type: DYN|interpreter' ; "$llvm/llvm-readelf" -h "$f" | grep -E 'Type:';; esac
    missing=$("$llvm/llvm-nm" -D --undefined-only "$f" | awk '{print $2}' | sed 's/@.*//' | sort -u | comm -23 - "$provided" || true)
    # Weak undefined (optional) symbols are not errors.
    weak=$("$llvm/llvm-nm" -D "$f" | awk '$1=="w"||$1=="v"{print $2}' | sed 's/@.*//' | sort -u)
    missing=$(comm -23 <(echo "$missing") <(echo "$weak") | grep -v '^$' || true)
    if [[ -n $missing ]]; then echo "  UNDEFINED:"; echo "$missing" | sed 's/^/    /'; fail=1; else echo "  no unresolved symbols"; fi
done
exit $fail
