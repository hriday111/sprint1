#!/usr/bin/env bash
#
# Build and run sprint1 (stepper driver + camera sweep).
#
# Usage: ./sprint1.sh {build|rebuild|driver|camera-sdk|run|help}
#
# Tunables (override via environment):
#   JOBS               parallel make jobs            (default: nproc; use JOBS=1 if the Makefile isn't parallel-safe)
#   STARTUP_TIMEOUT    max seconds to wait for the driver's pipe to appear   (default: 15)
#   STARTUP_SETTLE     extra seconds after the pipe appears                  (default: 1)
#   SHUTDOWN_TIMEOUT   seconds to let the driver home + exit after "-1"      (default: 30)
#   PIPE_WRITE_TIMEOUT seconds to wait when writing the stop signal          (default: 5)

set -Eeuo pipefail

# Always operate from the directory this script lives in, so ./build paths resolve.
ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
cd "$ROOT"

DRIVER_BIN="$ROOT/build/driver"
SWEEP_BIN="$ROOT/camera-sdk/build/bin/sweep_run"
PIPE="${SPRINT1_PIPE:-/run/sprint1/data_pipe}"
LOG_DIR="$ROOT/logs"
LOCK_FILE="${XDG_RUNTIME_DIR:-/tmp}/sprint1-run.lock"

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 1)}"
STARTUP_TIMEOUT="${STARTUP_TIMEOUT:-15}"
STARTUP_SETTLE="${STARTUP_SETTLE:-1}"
SHUTDOWN_TIMEOUT="${SHUTDOWN_TIMEOUT:-30}"
PIPE_WRITE_TIMEOUT="${PIPE_WRITE_TIMEOUT:-5}"

DRIVER_PID=""
DRIVER_LOG=""
SUDO_KEEPALIVE_PID=""

# ---------------------------------------------------------------- helpers

log()  { printf '\033[1;34m[%s]\033[0m %s\n' "$(date +%T)" "$*"; }
warn() { printf '\033[1;33m[%s] WARN:\033[0m %s\n' "$(date +%T)" "$*" >&2; }
die()  { printf '\033[1;31m[%s] ERROR:\033[0m %s\n' "$(date +%T)" "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $0 {build|rebuild|driver|camera-sdk|run|help}

  build       Build everything (make all)
  rebuild     Clean, then build everything
  driver      Build only the driver
  camera-sdk  Build only the camera SDK
  run         Start the driver, run the camera sweep, then home the stepper and stop
EOF
}

do_make() { make -j"$JOBS" "$@"; }

# The driver runs as root, so `kill -0` from this user fails with EPERM; check /proc instead.
driver_running() { [[ -n "$DRIVER_PID" && -d "/proc/$DRIVER_PID" ]]; }

# All descendants of a PID (sudo may sit between us and the real driver process).
descendants() {
    local child
    for child in $(pgrep -P "$1" 2>/dev/null); do
        descendants "$child"
        echo "$child"
    done
}

# Wait up to $1 seconds for the driver to exit. Returns 0 if it exited.
wait_for_driver_exit() {
    local deadline=$((SECONDS + $1))
    while driver_running && ((SECONDS < deadline)); do sleep 0.2; done
    ! driver_running
}

start_sudo_keepalive() {
    # Refresh sudo credentials so the final stop signal can't stall on a password
    # prompt after a long sweep. Dies with this script.
    local parent=$$
    ( while kill -0 "$parent" 2>/dev/null; do sudo -n true; sleep 50; done ) >/dev/null 2>&1 9>&- &
    SUDO_KEEPALIVE_PID=$!
}

stop_driver() {
    [[ -n "$DRIVER_PID" ]] || return 0

    if driver_running; then
        log "Sending stop signal (-1) to driver — homing stepper"
        # timeout: if the driver died, opening a FIFO for writing would block forever.
        if ! echo "-1" | sudo timeout "$PIPE_WRITE_TIMEOUT" tee -a "$PIPE" >/dev/null; then
            warn "Could not write stop signal to $PIPE"
        fi

        if ! wait_for_driver_exit "$SHUTDOWN_TIMEOUT"; then
            # Snapshot the whole tree first: once the top process dies its children get
            # re-parented and could no longer be found for the SIGKILL fallback.
            local tree
            tree="$(descendants "$DRIVER_PID" | tr '\n' ' ')$DRIVER_PID"
            warn "Driver still running after ${SHUTDOWN_TIMEOUT}s — sending SIGTERM"
            # shellcheck disable=SC2086
            sudo kill -TERM $tree 2>/dev/null || true
            sleep 5
            if ps -p "${tree// /,}" >/dev/null 2>&1; then
                warn "Driver ignored SIGTERM — sending SIGKILL (stepper may NOT be homed)"
                # shellcheck disable=SC2086
                sudo kill -KILL $tree 2>/dev/null || true
            fi
        fi
    fi

    local rc=0
    wait "$DRIVER_PID" 2>/dev/null || rc=$?
    DRIVER_PID=""
    if ((rc != 0)); then
        warn "Driver exited with code $rc — see $DRIVER_LOG"
    else
        log "Driver stopped cleanly"
    fi
}

cleanup() {
    local rc=$?
    trap - EXIT INT TERM
    stop_driver
    # shellcheck disable=SC2046
    [[ -n "$SUDO_KEEPALIVE_PID" ]] && kill $(descendants "$SUDO_KEEPALIVE_PID") "$SUDO_KEEPALIVE_PID" 2>/dev/null
    exit "$rc"
}

# ---------------------------------------------------------------- run

cmd_run() {
    # Only one run at a time — two drivers fighting over the stepper is bad news.
    exec 9>"$LOCK_FILE"
    flock -n 9 || die "Another run is already in progress (lock: $LOCK_FILE)"

    [[ -x "$DRIVER_BIN" ]] || die "Driver not built: $DRIVER_BIN (run: $0 build)"
    [[ -x "$SWEEP_BIN"  ]] || die "sweep_run not built: $SWEEP_BIN (run: $0 build)"

    log "Running sprint1 and homing stepper"
    sudo -v || die "sudo authentication failed"
    start_sudo_keepalive

    local pipe_preexisted=0
    [[ -e "$PIPE" ]] && pipe_preexisted=1

    mkdir -p "$LOG_DIR"
    DRIVER_LOG="$LOG_DIR/driver-$(date +%Y%m%d-%H%M%S).log"

    # From here on, any exit (error, Ctrl+C, kill) sends the stop signal and homes the stepper.
    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM

    log "Starting driver (log: $DRIVER_LOG)"
    # 9>&- : don't let the driver inherit the lock fd, or a stray driver would hold the lock forever.
    sudo "$DRIVER_BIN" >"$DRIVER_LOG" 2>&1 9>&- &
    DRIVER_PID=$!

    # Wait until the driver is actually ready instead of a blind sleep.
    local deadline=$((SECONDS + STARTUP_TIMEOUT))
    if ((pipe_preexisted)); then
        # A stale pipe from an earlier run can't tell us the driver is ready; give it the full window.
        warn "$PIPE already existed before start; waiting ${STARTUP_TIMEOUT}s instead of polling"
        while driver_running && ((SECONDS < deadline)); do sleep 0.2; done
    else
        while driver_running && [[ ! -e "$PIPE" ]] && ((SECONDS < deadline)); do sleep 0.1; done
        if driver_running && [[ ! -e "$PIPE" ]]; then
            die "Driver did not create $PIPE within ${STARTUP_TIMEOUT}s — see $DRIVER_LOG"
        fi
        sleep "$STARTUP_SETTLE"
    fi

    if ! driver_running; then
        tail -n 20 "$DRIVER_LOG" >&2 || true
        die "Driver exited during startup — see $DRIVER_LOG"
    fi
    log "Driver is up"

    log "Starting camera sweep"
    local sweep_rc=0
    sudo "$SWEEP_BIN" 9>&- || sweep_rc=$?
    if ((sweep_rc != 0)); then
        warn "sweep_run exited with code $sweep_rc"
    else
        log "Sweep finished"
    fi

    stop_driver
    log "Exiting"
    return "$sweep_rc"
}

# ---------------------------------------------------------------- main

case "${1:-}" in
    rebuild)
        log "Rebuilding driver and camera-sdk"
        make clean
        do_make all
        ;;
    driver)
        log "Building driver"
        do_make driver
        ;;
    camera-sdk)
        log "Building camera-sdk"
        do_make camera-sdk
        ;;
    build)
        log "Building all"
        do_make all
        ;;
    run)
        cmd_run
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac