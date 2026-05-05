#!/usr/bin/env bash
# Build and install the QMMP Discord Rich Presence plugin.
# Supports Flatpak QMMP and system-installed QMMP (Fedora, Arch, Debian/Ubuntu, openSUSE).

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

FLATPAK_APP_ID="com.ylsoftware.qmmp.Qmmp"
DEPS_DIR="$SCRIPT_DIR/.deps"

# ── Helper ───────────────────────────────────────────────────────────────────

die() { echo "ERROR: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "'$1' not found. Install it first."; }

# Print distro-appropriate install instructions.
print_install_hint() {
    echo ""
    echo "Install the required build dependencies, then re-run this script:"
    if command -v dnf &>/dev/null; then
        echo "  sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtwebsockets-devel qmmp-devel"
    elif command -v apt &>/dev/null; then
        echo "  sudo apt install cmake g++ qt6-base-dev libqt6websockets6-dev qmmp-dev"
    elif command -v pacman &>/dev/null; then
        echo "  sudo pacman -S cmake qt6-base qt6-websockets qmmp"
    elif command -v zypper &>/dev/null; then
        echo "  sudo zypper install cmake gcc-c++ qt6-base-devel qt6-websockets-devel qmmp-devel"
    elif [[ -f /etc/NIXOS ]]; then
        echo "  NixOS: add qmmp to your environment and build with:"
        echo "    nix-shell -p cmake qmmp qt6.qtbase qt6.qtwebsockets gcc"
    else
        echo "  Install: cmake, a C++ compiler, qt6-base dev headers,"
        echo "           qt6-websockets dev headers, and qmmp dev headers."
    fi
}

# Resolve the Flatpak 'files' directory for QMMP, checking both system-wide
# (/var/lib/flatpak) and per-user (~/.local/share/flatpak) installs.
find_flatpak_files() {
    local base
    for base in "/var/lib/flatpak" "$HOME/.local/share/flatpak"; do
        local path
        path=$(ls -d "$base/app/$FLATPAK_APP_ID/x86_64/stable/*/files" 2>/dev/null \
               | sort | tail -1)
        if [[ -n "$path" && -f "$path/lib/libqmmp.so" ]]; then
            echo "$path"
            return
        fi
    done
}

FLATPAK_FILES=$(find_flatpak_files)

# ── Detect mode ──────────────────────────────────────────────────────────────

if [[ -n "$FLATPAK_FILES" ]]; then
    MODE=flatpak
elif pkg-config --exists qmmp qmmpui 2>/dev/null; then
    MODE=system
else
    echo "Neither a Flatpak QMMP nor system QMMP with dev headers was found."
    print_install_hint
    exit 1
fi

echo "Mode: $MODE"
[[ "$MODE" == "flatpak" ]] && echo "Flatpak QMMP: $FLATPAK_FILES"

# ── Resolve QMMP headers (Flatpak mode) ──────────────────────────────────────
# In order of preference:
#   1. Already extracted into .deps/
#   2. RPM file in the repo directory  (Fedora/RHEL)
#   3. DEB file in the repo directory  (Debian/Ubuntu)
#   4. System-installed headers via pkg-config (Arch, openSUSE, etc.)

if [[ "$MODE" == "flatpak" ]]; then
    if [[ -d "$DEPS_DIR/usr/include/qmmp" ]]; then
        QMMP_INCLUDE_DIR="$DEPS_DIR/usr/include"

    elif DEVEL_RPM=$(ls "$SCRIPT_DIR"/qmmp-devel-*.x86_64.rpm 2>/dev/null | head -1) && [[ -n "$DEVEL_RPM" ]]; then
        need rpm2cpio
        need cpio
        echo "Extracting headers from $DEVEL_RPM …"
        mkdir -p "$DEPS_DIR"
        (cd "$DEPS_DIR" && rpm2cpio "$DEVEL_RPM" | cpio -idm 2>/dev/null)
        QMMP_INCLUDE_DIR="$DEPS_DIR/usr/include"

    elif DEVEL_DEB=$(ls "$SCRIPT_DIR"/qmmp-dev_*.deb "$SCRIPT_DIR"/qmmp_*_amd64.deb 2>/dev/null | head -1) && [[ -n "$DEVEL_DEB" ]]; then
        need dpkg-deb
        echo "Extracting headers from $DEVEL_DEB …"
        mkdir -p "$DEPS_DIR"
        dpkg-deb -x "$DEVEL_DEB" "$DEPS_DIR"
        QMMP_INCLUDE_DIR=$(find "$DEPS_DIR" -type d -name "qmmp" 2>/dev/null | head -1 | xargs dirname)
        [[ -n "$QMMP_INCLUDE_DIR" ]] || die "Could not locate qmmp headers inside $DEVEL_DEB"

    elif pkg-config --exists qmmp 2>/dev/null; then
        echo "Using system QMMP headers with Flatpak libraries."
        QMMP_INCLUDE_DIR=$(pkg-config --variable=includedir qmmp)

    else
        echo "QMMP dev headers not found. Provide one of:"
        echo "  • qmmp-devel-*.x86_64.rpm  (Fedora/RHEL)"
        echo "  • qmmp-dev_*.deb            (Debian/Ubuntu)"
        echo "  • System qmmp dev package   (pkg-config must find it)"
        print_install_hint
        exit 1
    fi
fi

# ── Configure cmake args ──────────────────────────────────────────────────────

CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release)

if [[ "$MODE" == "flatpak" ]]; then
    CMAKE_ARGS+=(
        "-DQMMP_INCLUDE_DIR=$QMMP_INCLUDE_DIR"
        "-DQMMP_LIB_DIR=$FLATPAK_FILES/lib"
        "-DPLUGIN_DIR=$FLATPAK_FILES/lib/qmmp-2.3"
    )
fi

# ── Build ─────────────────────────────────────────────────────────────────────
# Recommended: qt6-qtwebsockets-devel (or distro equivalent) for WebSocket fallback.

need cmake

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

echo ""
echo "Build succeeded: $BUILD_DIR/libdiscordrichpresence.so"

# ── Install ───────────────────────────────────────────────────────────────────

if [[ "$MODE" == "flatpak" ]]; then
    DEST="$FLATPAK_FILES/lib/qmmp-2.3/General"

    # Determine whether the Flatpak is user-owned (no sudo needed) or system-wide.
    if [[ "$FLATPAK_FILES" == "$HOME/.local/share/flatpak"* ]]; then
        echo "Installing to user Flatpak plugin directory…"
        cp "$BUILD_DIR/libdiscordrichpresence.so" "$DEST/"
    else
        echo "Installing to system Flatpak plugin directory (needs sudo)…"
        sudo cp "$BUILD_DIR/libdiscordrichpresence.so" "$DEST/"
    fi
    echo "Installed: $DEST/libdiscordrichpresence.so"

    # Grant QMMP Flatpak access to the Discord IPC sockets.
    echo ""
    echo "Granting QMMP Flatpak access to Discord IPC sockets…"
    PERM_OK=true
    for i in $(seq 0 9); do
        flatpak override --user --filesystem="xdg-run/discord-ipc-$i" "$FLATPAK_APP_ID" 2>/dev/null || PERM_OK=false
    done
    if $PERM_OK; then
        echo "Permissions granted."
    else
        echo "Could not set permissions automatically. Run manually:"
        for i in $(seq 0 9); do
            echo "  flatpak override --user --filesystem=xdg-run/discord-ipc-$i $FLATPAK_APP_ID"
        done
    fi

    # If Discord/Vesktop is also a Flatpak, grant it the same socket permissions so
    # it can create the IPC socket where QMMP can reach it.
    echo ""
    echo "Checking for Flatpak Discord clients…"
    FOUND_DISCORD=false
    for DISCORD_APP in dev.vencord.Vesktop com.discordapp.Discord com.discordapp.DiscordCanary; do
        if flatpak info "$DISCORD_APP" &>/dev/null; then
            FOUND_DISCORD=true
            echo "  Found $DISCORD_APP — granting discord-ipc socket access…"
            for i in $(seq 0 9); do
                flatpak override --user --filesystem="xdg-run/discord-ipc-$i" "$DISCORD_APP" 2>/dev/null
            done
            echo "  Done. Restart $DISCORD_APP for the change to take effect."
        fi
    done
    $FOUND_DISCORD || echo "  No Flatpak Discord clients found (using native install — no action needed)."
else
    echo "Installing (needs sudo)…"
    sudo cmake --install "$BUILD_DIR"
fi

echo ""
echo "Done. Launch QMMP, go to Plugins → General → Discord Rich Presence,"
echo "enable it and enable Rich Presence in your Discord client settings."
echo "(In Vesktop: Settings → Vesktop Settings → Enable Rich Presence)"
