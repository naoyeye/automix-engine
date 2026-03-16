#!/bin/bash
set -euo pipefail

# One-click switch to native arm64 toolchain on Apple Silicon.
# - Ensures arm64 Homebrew is available
# - Installs required dependencies
# - Rebuilds cmake-build/libautomix.a for arm64
# - Requires Essentia to be available in arm64 pkg-config

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/cmake-build"

log() { echo "[arm64-switch] $*"; }
err() { echo "[arm64-switch] ERROR: $*" >&2; }

if [[ "$(uname -s)" != "Darwin" ]]; then
  err "This script is only for macOS."
  exit 1
fi

CURRENT_ARCH="$(uname -m)"
HW_ARM64_CAPABLE="$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)"

# If this shell runs under Rosetta on Apple Silicon, transparently re-exec as arm64.
if [[ "$CURRENT_ARCH" == "x86_64" && "$HW_ARM64_CAPABLE" == "1" ]]; then
  if [[ "${AUTOMIX_ARM64_REEXEC:-0}" != "1" ]]; then
    log "Detected Rosetta shell (x86_64). Re-running script as arm64 ..."
    export AUTOMIX_ARM64_REEXEC=1
    exec /usr/bin/arch -arm64 /bin/bash "$0" "$@"
  fi
fi

if [[ "$(uname -m)" != "arm64" ]]; then
  err "Current shell is not arm64. On Apple Silicon, try a native Terminal and run again."
  exit 1
fi

if [[ "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage: ./scripts/switch_arm64.sh

What it does:
1) Ensures /opt/homebrew (arm64 Homebrew) exists
2) Installs build/runtime dependencies via arm64 brew
3) Rebuilds cmake-build as arm64
4) Verifies libautomix.a is arm64

Notes:
- `essentia` is required. If arm64 pkg-config cannot resolve it, the script exits.
- You may still need to restart Xcode and clean build folder after switching.
EOF
  exit 0
fi

if [[ ! -x /opt/homebrew/bin/brew ]]; then
  log "arm64 Homebrew not found. Installing to /opt/homebrew ..."
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
fi

BREW_BIN="/opt/homebrew/bin/brew"
if [[ ! -x "$BREW_BIN" ]]; then
  err "Failed to locate arm64 brew at /opt/homebrew/bin/brew"
  exit 1
fi

BREW_PREFIX="$(/usr/bin/arch -arm64 "$BREW_BIN" --prefix)"
log "Using arm64 Homebrew: $BREW_PREFIX"

export PATH="$BREW_PREFIX/bin:$BREW_PREFIX/sbin:$PATH"
export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig:$BREW_PREFIX/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$BREW_PREFIX/lib/pkgconfig:$BREW_PREFIX/share/pkgconfig:/usr/lib/pkgconfig"
export CMAKE_PREFIX_PATH="$BREW_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

log "Updating Homebrew metadata ..."
/usr/bin/arch -arm64 "$BREW_BIN" update

log "Installing arm64 dependencies ..."
/usr/bin/arch -arm64 "$BREW_BIN" install \
  cmake pkg-config ffmpeg sqlite chromaprint rubberband fftw libsamplerate libyaml taglib eigen python || true

# Install missing formulae if install command above partially failed.
for f in cmake pkg-config ffmpeg sqlite chromaprint rubberband fftw libsamplerate libyaml taglib eigen python; do
  if ! /usr/bin/arch -arm64 "$BREW_BIN" list --versions "$f" >/dev/null 2>&1; then
    log "Retry install formula: $f"
    /usr/bin/arch -arm64 "$BREW_BIN" install "$f"
  fi
done

if ! /usr/bin/arch -arm64 "$BREW_PREFIX/bin/pkg-config" --exists essentia 2>/dev/null; then
  err "arm64 essentia not found via $BREW_PREFIX/bin/pkg-config"
  err "Build halted because Essentia is required by this project."
  err "Install Essentia for arm64 (ensure $BREW_PREFIX/lib/pkgconfig/essentia.pc exists), then rerun."
  exit 1
fi

log "Recreating arm64 build directory ..."
rm -rf "$BUILD_DIR"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DENABLE_ESSENTIA=ON \
  -DPKG_CONFIG_EXECUTABLE="$BREW_PREFIX/bin/pkg-config"

cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

if [[ ! -f "$BUILD_DIR/libautomix.a" ]]; then
  err "Build finished but $BUILD_DIR/libautomix.a is missing."
  exit 1
fi

ARCH_INFO="$(lipo -info "$BUILD_DIR/libautomix.a" 2>/dev/null || true)"
if [[ "$ARCH_INFO" != *"arm64"* ]]; then
  err "libautomix.a is not arm64: $ARCH_INFO"
  exit 1
fi

log "Success. libautomix.a architecture: $ARCH_INFO"
log "Next: reopen Xcode and Build (or Product > Clean Build Folder first)."
