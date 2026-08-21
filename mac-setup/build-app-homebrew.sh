#!/usr/bin/env bash
#
# Build Xournal++.app against a Homebrew GTK stack.
#
# This is NOT a replacement for build-app.sh. That script uses jhbuild and
# gtk-mac-bundler to copy every GTK dylib into the bundle, producing something
# redistributable. This one deliberately does not: the .app it makes links
# against the Homebrew prefix and therefore only runs on a machine that has
# these formulae installed. In exchange it builds in minutes rather than hours,
# and it has far fewer moving parts.
#
# Use it for a personal build of a fork. Use build-app.sh to ship to anyone else.
#
# Note on Gatekeeper: an ad-hoc signature is applied at the end. Without ANY
# signature an arm64 binary will not execute at all. With an ad-hoc one it runs
# locally, but it is still not notarized -- so if the bundle ever picks up a
# com.apple.quarantine attribute (by being downloaded, or zipped, sent
# somewhere and brought back), macOS refuses it with "is damaged and can't be
# opened". That message means quarantine, NOT corruption. Clear it with:
#
#     xattr -dr com.apple.quarantine ~/Applications/Xournal++.app
#
# Build speed: the project's own CMakeLists precompile GTK's headers, which is
# most of what a file costs to compile. The other half is ccache, which is not
# automatic -- pass -DCMAKE_CXX_COMPILER_LAUNCHER=ccache when configuring, as
# the message below does, and set it up once with:
#
#     brew install ccache
#     ccache --set-config=sloppiness=pch_defines,time_macros
#
# That sloppiness setting is not optional. Without it ccache refuses to cache
# any compilation that uses a precompiled header, which here means all of them.
#
# Usage:
#   mac-setup/build-app-homebrew.sh [build-dir] [output-dir]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build}"
OUT_DIR="${2:-$REPO_ROOT/mac-setup}"
APP="$OUT_DIR/Xournal++.app"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

BREW_PREFIX="$(brew --prefix)"

die() { echo "build-app-homebrew: $*" >&2; exit 1; }

[ -x "$BUILD_DIR/xournalpp" ] || die "no binary at $BUILD_DIR/xournalpp -- build first:
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
        -DCMAKE_PREFIX_PATH=$BREW_PREFIX -DENABLE_FLOAT_FROM_CHARS=OFF \\
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  cmake --build build -j"

echo "==> clean"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# --- payload ---------------------------------------------------------------
# Take the payload from CMake's own install rules rather than copying source
# directories by hand. Several data files -- ui/toolbar.ini most importantly --
# are GENERATED into the build tree from resources-templates/, so a bundle
# assembled from the source tree comes up with no toolbars at all.
echo "==> stage install"
cmake --install "$BUILD_DIR" --prefix "$STAGE" > /dev/null

# Util::getDataPath() on macOS is getExePath().parent_path() / "Resources", and
# getExePath() already returns the DIRECTORY holding the executable. So with the
# binary at Contents/MacOS/xournalpp the data belongs at Contents/Resources --
# the ordinary bundle layout, matching the official build.
echo "==> assemble bundle"
cp "$STAGE/bin/xournalpp" "$APP/Contents/MacOS/xournalpp"
cp -R "$STAGE/share/xournalpp/." "$APP/Contents/Resources/"
mkdir -p "$APP/Contents/Resources/share"
cp -R "$STAGE/share/locale" "$APP/Contents/Resources/share/"

if [ -f "$REPO_ROOT/mac-setup/icon/xournalpp.icns" ]; then
  cp "$REPO_ROOT/mac-setup/icon/xournalpp.icns" "$APP/Contents/Resources/"
fi

# macOS reports Cmd as the primary modifier, so menu accelerators must be Meta
# rather than Ctrl or every shortcut in the menubar is wrong. build-app.sh does
# the same rewrite. The '' argument keeps BSD sed from leaving a .xml-e backup
# beside the real file (the official bundle ships one by accident).
if [ -f "$APP/Contents/Resources/ui/mainmenubar.xml" ]; then
  echo "==> rewrite Ctrl -> Meta in mainmenubar.xml"
  sed -i '' -e 's/Ctrl/Meta/g' "$APP/Contents/Resources/ui/mainmenubar.xml"
fi

# --- launcher --------------------------------------------------------------
# A COMPILED launcher, not a shell script. LaunchServices refuses to spawn a
# bundle whose CFBundleExecutable is a script -- it fails with RBSRequestError
# code 5 / POSIX 162 "Launchd job spawn failed", while running the same script
# from a terminal works fine, which makes it look like a permissions problem.
# This is why upstream's xournalpp-wrapper is a Mach-O binary too.
#
# Its whole job is to set the environment GTK needs before exec'ing the real
# binary: a double-clicked app inherits essentially nothing, so none of this can
# be left to a shell profile.
echo "==> compile launcher"
cat > "$STAGE/launcher.c" <<'LAUNCHER'
#include <libgen.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int main(int argc, char** argv) {
    char self[4096];
    uint32_t size = sizeof(self);
    if (_NSGetExecutablePath(self, &size) != 0) {
        fprintf(stderr, "xournalpp-launch: executable path too long\n");
        return 1;
    }

    char* dir = dirname(self);

    char buf[8192];
    const char* existing = getenv("XDG_DATA_DIRS");
    snprintf(buf, sizeof(buf), "%s/share:%s", BREW_PREFIX,
             (existing && *existing) ? existing : "/usr/local/share:/usr/share");
    setenv("XDG_DATA_DIRS", buf, 1);

    snprintf(buf, sizeof(buf), "%s/share/glib-2.0/schemas", BREW_PREFIX);
    setenv("GSETTINGS_SCHEMA_DIR", buf, 1);

    /* Only set the loader cache if it is really there. Unset, gdk-pixbuf uses
       its built-in path; set to a MISSING file it loads no loaders at all and
       every icon silently disappears. */
    snprintf(buf, sizeof(buf), "%s/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache", BREW_PREFIX);
    if (exists(buf)) {
        setenv("GDK_PIXBUF_MODULE_FILE", buf, 1);
    }

    char target[4096];
    snprintf(target, sizeof(target), "%s/xournalpp", dir);

    /* Hand the real binary its OWN name as argv[0]. execv would otherwise keep
       this launcher's argv[0], leaving the running process called
       "xournalpp-launch" -- which breaks `pgrep -x xournalpp` and anything else
       matching on the process name, even though the app itself works. */
    argv[0] = target;

    execv(target, argv);
    perror("xournalpp-launch: exec failed");
    return 1;
}
LAUNCHER

clang -O2 -Wall -o "$APP/Contents/MacOS/xournalpp-launch" \
      -DBREW_PREFIX="\"$BREW_PREFIX\"" "$STAGE/launcher.c"

# --- Info.plist ------------------------------------------------------------
echo "==> write Info.plist"
# The project() call spells it `VERSION 1.3.6`, unquoted.
VERSION="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\)[[:space:]]*$/\1/p' \
           "$REPO_ROOT/CMakeLists.txt" | head -1)"
VERSION="${VERSION:-0.0.0}"

cp "$REPO_ROOT/mac-setup/Info.plist" "$APP/Contents/Info.plist"
plutil -replace CFBundleExecutable -string "xournalpp-launch" "$APP/Contents/Info.plist"
plutil -replace CFBundleShortVersionString -string "$VERSION" "$APP/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$VERSION" "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# --- signature -------------------------------------------------------------
# arm64 refuses to execute an unsigned Mach-O outright, so this is required, not
# cosmetic. Sign the inner binary before the bundle: signing the bundle seals
# the contents, so anything modified afterwards invalidates it.
echo "==> ad-hoc sign"
codesign --force --sign - "$APP/Contents/MacOS/xournalpp"
codesign --force --sign - "$APP/Contents/MacOS/xournalpp-launch"
codesign --force --sign - "$APP"
codesign --verify --strict "$APP" && echo "    signature ok"

echo
echo "built: $APP  (version $VERSION)"
echo "install with:"
echo "  rm -rf ~/Applications/Xournal++.app && cp -R '$APP' ~/Applications/"
