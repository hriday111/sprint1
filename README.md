# Sprint1 — Prism scan controller {#mainpage}

Sprint1 runs a Raspberry Pi–mounted A4988 stepper, laser, and endstop (“the black box”) while `camera-sdk/` grabs frames from a Thorlabs DCC1545M (IDS uEye) and records ROI intensities. The two processes talk over named FIFOs under `/run/sprint1/`. This guide is for engineers operating the rig on a Pi 4 with WiringPi and the uEye SDK.

## Documentation

Published API docs: [https://hriday111.github.io/sprint1](https://hriday111.github.io/sprint1)

Local HTML: `./docs/generate.sh` then open `docs/html/index.html`.

## Prerequisites

- Raspberry Pi 4 with WiringPi (`-lwiringPi`)
- Root (or equivalent GPIO) privileges for `servo_calib` (hardware PWM on BCM 18)
- IDS uEye SDK (`libueye_api`); OpenCV 4 for `calibrate` / `calibrate2`
- Hardware wired per the pin table below
- Writable `/run/sprint1/` (FIFOs), `/var/lib/sprint1/` (`rois.json`, `rois_interp.json`, `saved.prm`), and a local `logs/` directory (created on start)
- A sample `saved.prm` lives in the repo; ROI files are produced by the calibrate tools

## Quickstart

```bash
# Terminal 1 — motor + laser daemon
sudo make
sudo ./build/servo_calib
# Logs: logs/servo_calib-<epoch>.log (+ stderr)

# Terminal 2 — place ROIs (after lens moves)
cd camera-sdk && make
mkdir -p tmp
./build/bin/calibrate 0.1
# Inspect tmp/detected.png — green ellipses on the three dark discs

# Optional — two-point interp model (leaves servo_calib running)
./build/bin/calibrate2 0.1 400 420
# Writes /var/lib/sprint1/rois_interp.json and tmp/interp_intensity.csv

# Intensity sweep (servo_calib still running)
./build/bin/sweep_run /var/lib/sprint1/rois.json 0.1 ../sensor_data.csv
```

On success, `sensor_data.csv` has one row per step: `Angle_deg` plus one mean column per ROI (ellipse mask).

**Note:** `sweep_run` sends `-1` on the command FIFO when it finishes, which **exits** `servo_calib`. `calibrate2` closes the FIFO **without** `-1` and leaves the daemon running.

## Architecture

| Component | Role |
|-----------|------|
| `build/servo_calib` | GPIO, home on endstop, serve commands, publish position, logging/crash hooks |
| `camera-sdk/sweep_run` | Move start→stop, grab crop, average elliptical ROIs, write CSV |
| `camera-sdk/calibrate` | Detect discs once at current pose; write `rois.json` |
| `camera-sdk/calibrate2` | Detect at two servo positions; write interp model + check CSV |
| `camera-sdk/servo_fifo.*` | Shared FIFO client (open/send/move; close vs shutdown) |
| `camera-sdk/roi_interp.*` | Linear lerp of ellipse geometry vs step |
| `logger.c` / `crash.c` | Shared file+console logging and fatal-signal / core-dump setup |

| Path | Direction | Purpose |
|------|-----------|---------|
| `/run/sprint1/data_pipe` | client → servo | newline-terminated commands |
| `/run/sprint1/pos_pipe` | servo → client | position after command `4000` |

Keep the command FIFO open for the whole sweep. Opening and closing per command races with `servo_calib`’s reopen and can cause `EPIPE`.

## Logging and crashes

Every binary that calls `sprint1_logging_init` writes:

- **File:** `logs/<app>-<unix_epoch>.log` (1 MiB rotate, 5 backups, DEBUG level)
- **Console:** same lines on stderr
- **Auto-flush:** every 100 ms while logging

On SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL/SIGQUIT/SIGSYS/SIGTRAP, the process logs where the core dump is expected (from `/proc/sys/kernel/core_pattern`), flushes, then re-raises for a kernel dump. `RLIMIT_CORE` is raised to unlimited and the process is marked dumpable.

## Pinout

BCM pins are defined in `gpio_pins.h`. Enable is active LOW; endstop is active HIGH; laser uses hardware PWM on GPIO 18.

| Signal | GPIO | Typical wire |
|--------|------|--------------|
| Enable | 20 | Green |
| MS1 / MS2 / MS3 | 13 / 19 / 26 | Blue / Purple / Grey |
| Step / Dir | 27 / 22 | White / Black |
| Laser / Endstop | 18 / 17 | Blue→White / Grey |

Microstepping is fixed at 1/16. Step half-period: `STEP_DELAY_US` (20 ms).

## Command protocol

Send ASCII lines on `data_pipe`. Integer lines are commands; `bN` sets laser brightness (0–255).

| Input | Meaning |
|-------|---------|
| `0`–`1000` | Absolute goto step position |
| `1001` / `1002` | Laser on (full PWM) / off |
| `1003` | Disable motor (`CMD_DISABLE`; `CMD_RESET` shares this value and is unreachable) |
| `1004` | Enable motor |
| `1006` / `1007` | Single step forward / back |
| `1008` | Refresh lid state (`PIN_LID`, unused when `-1`) |
| `2000` | Reload `/var/lib/sprint1/saved.prm` |
| `4000` | Log position; write to `pos_pipe` if a reader is waiting |
| `-1` | Exit `servo_calib` |

**Example** — laser on and read position (`servo_calib` must be running):

```bash
exec 3>/run/sprint1/data_pipe
printf 'b255\n1001\n4000\n' >&3
timeout 1 cat /run/sprint1/pos_pipe
```

Angle conversion uses **0.1125° per step**. Homing lands near step `400 + 34` when the endstop trips during bookkeeping (`CMD_POS_ZERO` + `DELTA`).

## Config files

**`/var/lib/sprint1/saved.prm`** — three integers `start stop prep` (example: `345 395 373`).

- Each value must be in **0–999** (`servo_calib` rejects others).
- The loader reads at most **31** bytes before a NUL; keep the line short.
- Both `servo_calib` and `sweep_run` use this path.

**`/var/lib/sprint1/rois.json`** — up to 8 elliptical blobs in crop coordinates (`CAM_CROP_*`: 600×740 at (350, 60)). Fields: `name`, `cx`, `cy`, `rx`, `ry`, `angle`. Intensity is the **ellipse mask** mean (legacy `{x,y,w,h}` still loads as an axis-aligned ellipse).

**`/var/lib/sprint1/rois_interp.json`** — two-point model from `calibrate2` (`pos0`/`pos1` + `at0`/`at1` ellipse arrays). Used to lerp blob geometry vs step; `sweep_run` still reads a single `rois.json` today.

## Common pitfalls

| Symptom | Fix |
|---------|-----|
| `no reader (is servo_calib running?)` | Start `sudo ./build/servo_calib` first |
| `EPIPE` mid-sweep | Keep one `O_WRONLY` fd open for the whole scan |
| `position: no reply after retries` | Open `pos_pipe` for read **before** sending `4000` |
| Ellipses miss the discs | Run `./calibrate` or `./calibrate2` after any lens move |
| ROI saturation warning | Lower exposure or `b` brightness |
| `loadParams: unsupported values` | Keep start/stop/prep in 0–999 |
| Goto fails after boot | Confirm endstop on GPIO 17; enable before absolute moves |
| Daemon gone after sweep | Expected for `sweep_run`; use `calibrate2` if you need the daemon to stay up |
| No `logs/` file | Process must start in a writable cwd (creates `logs/`) |

## Why calibrate once (or twice)?

Detection runs only in the calibrate tools. Measurement averages ellipse masks so `sweep_run` stays OpenCV-free. After any optical change, recalibrate. For pose-dependent drift across a scan window, use `calibrate2` and inspect `tmp/interp_intensity.csv` (interp vs fixed means). Full camera API: [camera-sdk/README.md](camera-sdk/README.md).

> **Verify on hardware:** uEye device permissions, `/var/lib/sprint1/` provisioning, and that `CMD_DISABLE`/`CMD_RESET` both being `1003` matches intended behavior.
