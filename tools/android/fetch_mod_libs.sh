#!/usr/bin/env bash
# Libraries for the app's Mods menu (ModArchive.java), which unpacks .zip and .7z mod archives with
# Apache Commons Compress (Apache 2.0), its commons-io, commons-codec and commons-lang3, and xz (the
# 7z LZMA/LZMA2 coders).
# Resolved with a pinned Apache Maven into .local-deps/android/mod-libs/jars; build_frontend.sh dexes the
# jars into the APK. (The Steam sign-in's libraries, JavaSteam and its dependencies, are not fetched any
# more: that dialog went away with the Linux runtime's Lossless Scaling layer.)
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
deps=$PWD/.local-deps/android
out=$deps/mod-libs
MAVEN_VERSION=3.9.11
MAVEN_SHA512=bcfe4fe305c962ace56ac7b5fc7a08b87d5abd8b7e89027ab251069faebee516b0ded8961445d6d91ec1985dfe30f8153268843c89aa392733d1a3ec956c9978
XZ=1.12 COMPRESS=1.26.1 LANG3=3.20.0
if [[ ! -x $deps/maven/bin/mvn ]]; then
    curl -sfL -o "$deps/maven.tar.gz" \
        "https://archive.apache.org/dist/maven/maven-3/$MAVEN_VERSION/binaries/apache-maven-$MAVEN_VERSION-bin.tar.gz"
    echo "$MAVEN_SHA512  $deps/maven.tar.gz" | sha512sum -c --quiet -
    mkdir -p "$deps/maven" && tar -xzf "$deps/maven.tar.gz" -C "$deps/maven" --strip-components=1
    rm "$deps/maven.tar.gz"
fi
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cat > "$work/pom.xml" <<POM
<project xmlns="http://maven.apache.org/POM/4.0.0">
  <modelVersion>4.0.0</modelVersion>
  <groupId>local.bloodborne</groupId><artifactId>mod-libs</artifactId><version>1</version>
  <dependencies>
    <dependency><groupId>org.tukaani</groupId><artifactId>xz</artifactId><version>$XZ</version></dependency>
    <dependency><groupId>org.apache.commons</groupId><artifactId>commons-compress</artifactId><version>$COMPRESS</version></dependency>
    <dependency><groupId>org.apache.commons</groupId><artifactId>commons-lang3</artifactId><version>$LANG3</version></dependency>
  </dependencies>
</project>
POM
rm -rf "$out"; mkdir -p "$out/jars"
"$deps/maven/bin/mvn" -q -B ${BB_MVN_OFFLINE:+-o} -f "$work/pom.xml" -Dmaven.repo.local="$deps/m2" \
    dependency:copy-dependencies -DoutputDirectory="$work/deps" -DincludeScope=runtime
cp "$work"/deps/*.jar "$out/jars/"
ls "$out/jars" > "$out/jars.txt"
echo "Mod libraries: $(wc -l < "$out/jars.txt") jars in $out"
