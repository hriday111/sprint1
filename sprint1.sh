#!/usr/bin/env bash
#
# Build and run sprint1 (stepper driver + camera sweep).
#
# Usage: ./sprint1.sh {build|rebuild|driver|camera-sdk|run|help}
#
# Layout (after cmake -S . -B build):
#   build/src/driver/driver
#   build/src/camera-sdk/sweep_run

set -euo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
cd "$ROOT"

BUILD_DIR="$ROOT/build"
DRIVER_BIN="$BUILD_DIR/src/driver/driver"
SWEEP_BIN="$BUILD_DIR/src/camera-sdk/sweep_run"
PIPE="${SPRINT1_PIPE:-/run/sprint1/data_pipe}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 1)}"

usage() {
    cat <<EOF
Usage: $0 {build|rebuild|driver|camera-sdk|run|help}

  build       Configure (if needed) and build everything
  rebuild     Wipe build/, reconfigure, and build everything
  driver      Build only the driver
  camera-sdk  Build camera tools (sweep_run, calibrate, calibrate2)
  run         Start the driver, run sweep_run, then home the stepper and stop
EOF
}

cmake_configure() {
    cmake -S "$ROOT" -B "$BUILD_DIR"
}

cmake_build() {
    cmake --build "$BUILD_DIR" -j"$JOBS" "$@"
}

ensure_built() {
    [[ -x "$DRIVER_BIN" ]] || {
        echo "Driver not built: $DRIVER_BIN (run: $0 build)" >&2
        exit 1
    }
    [[ -x "$SWEEP_BIN" ]] || {
        echo "sweep_run not built: $SWEEP_BIN (run: $0 build)" >&2
        exit 1
    }
}

case "${1:-}" in
    rebuild)
        echo "Rebuilding driver and camera-sdk"
        rm -rf "$BUILD_DIR"
        cmake_configure
        cmake_build
        ;;

    driver)
        echo "Building driver"
        cmake_configure
        cmake_build --target driver
        ;;

    camera-sdk)
        echo "Building camera-sdk"
        cmake_configure
        cmake_build --target sweep_run --target calibrate --target calibrate2
        ;;

    build)
        echo "Building all"
        cmake_configure
        cmake_build
        ;;

    run)
        echo "Running sprint1 and homing stepper"
        ensure_built
        sudo -v

        sudo "$DRIVER_BIN" >/dev/null 2>&1 &
        BG_PID=$!
        sleep 5
        sudo "$SWEEP_BIN"

        echo "-1" | sudo tee -a "$PIPE" >/dev/null
        echo "Exiting"
        wait "$BG_PID" || true
        ;;

    help|-h|--help)
        usage
        ;;

    *)
        usage >&2
        exit 1
        ;;
esac
