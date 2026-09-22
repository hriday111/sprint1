# Sprint1 — Prism scan controller {#mainpage}

Sprint1 runs a Raspberry Pi–mounted A4988 stepper, laser, and endstop (“the black box”) while `camera-sdk/` grabs frames from a Thorlabs DCC1545M (IDS uEye) and records ROI intensities. The two processes talk over named FIFOs under `/run/sprint1/`. This guide is for engineers operating the rig on a Pi 4 with WiringPi and the uEye SDK.

## Documentation

Published API docs: [https://hriday111.github.io/sprint1](https://hriday111.github.io/sprint1)

Local HTML: `./docs/generate.sh` then open `docs/html/index.html`.

## Getting started (normal run)

**ROI calibration is already done** for the current optics (`rois.json` under `/var/lib/sprint1/`). For day-to-day measurements you only need the motor driver and the sweep tool:

```bash
# Terminal 1 — start the stepper / laser driver
sudo make
sudo ./build/servo_calib

# Terminal 2 — main intensity measurements (interactive TUI)
cd camera-sdk && make
./build/bin/sweep_run
# or with args: ./build/bin/sweep_run /var/lib/sprint1/rois.json 0.1 ../sensor_data.csv
```

`sweep_run` opens a disease-assignment TUI (D1–D10 for blob 1 and blob 2), then Start/Reset for the scan. It leaves `servo_calib` running when you quit (FIFO closed without `-1`).

### If you must recalibrate

Only recalibrate after moving the lens or remounting optics. Prefer `calibrate2`. If detection looks wrong, **try several exposures** and pick the best overlays:

```bash
cd camera-sdk
mkdir -p tmp
# Try these exposures and compare tmp/detected_*.png
./build/bin/calibrate2 0.20 400 420
./build/bin/calibrate2 0.14 400 420
./build/bin/calibrate2 0.10 400 420
```

Green ellipses must sit on the three dark discs. Avoid long exposures with a bright laser — clipped discs break detection. See [camera-sdk/RECALIBRATE.md](camera-sdk/RECALIBRATE.md).

## Prerequisites

- Raspberry Pi 4 with WiringPi (`-lwiringPi`)
- Root (or equivalent GPIO) privileges for `servo_calib` (hardware PWM on BCM 18)
- IDS uEye SDK (`libueye_api`); OpenCV 4 for `calibrate` / `calibrate2`
- Hardware wired per the pin table below
- Writable `/run/sprint1/` (FIFOs), `/var/lib/sprint1/` (`rois.json`, `rois_interp.json`, `saved.prm`), and a local `logs/` directory (created on start)

## Architecture

| Component | Role |
|-----------|------|
| `build/servo_calib` | GPIO, home on endstop, serve commands, publish position, logging/crash hooks |
| `camera-sdk/build/bin/sweep_run` | Main measurements: TUI + move start→stop, ellipse ROI means → CSV |
| `camera-sdk/sweep_tui.*` | Disease picker (D1–D10) and Start/Reset UI |
| `camera-sdk/calibrate` | One-shot detect at current pose → `rois.json` |
| `camera-sdk/calibrate2` | Detect at two servo positions → interp model + check CSV |
| `camera-sdk/servo_fifo.*` | Shared FIFO client (open/send/move/enable; close vs shutdown) |
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

On SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL/SIGQUIT/SIGSYS/SIGTRAP, the process logs where the core dump is expected (from `/proc/sys/kernel/core_pattern`), flushes, then re-raises for a kernel dump.

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

Angle conversion uses **0.1125° per step**. Homing lands near step `400 + 34` when the endstop trips (`CMD_POS_ZERO` + `DELTA`).

## Config files

**`/var/lib/sprint1/saved.prm`** — three integers `start stop prep` (example: `345 395 373`). Each value must be in **0–999**. Both `servo_calib` and `sweep_run` use this path.

**`/var/lib/sprint1/rois.json`** — elliptical blobs (`name`, `cx`, `cy`, `rx`, `ry`, `angle`) in crop coords (`CAM_CROP_*`: 600×740 at (350, 60)). Intensity is the ellipse-mask mean.

**`/var/lib/sprint1/rois_interp.json`** — two-point model from `calibrate2` (`pos0`/`pos1` + `at0`/`at1`).

## Common pitfalls

| Symptom | Fix |
|---------|-----|
| `no reader (is servo_calib running?)` | Start `sudo ./build/servo_calib` first |
| `EPIPE` mid-sweep | Keep one `O_WRONLY` fd open for the whole scan |
| Ellipses miss the discs | Recalibrate with `calibrate2` at 0.20 / 0.14 / 0.10 ms and pick the best |
| ROI saturation warning | Lower exposure or `b` brightness |
| `loadParams: unsupported values` | Keep start/stop/prep in 0–999 |
| Goto fails after boot | Confirm endstop on GPIO 17; enable before absolute moves |
| No `logs/` file | Process must start in a writable cwd |

Full camera API: [camera-sdk/README.md](camera-sdk/README.md).

> **Verify on hardware:** uEye permissions, `/var/lib/sprint1/` provisioning, and that `CMD_DISABLE`/`CMD_RESET` both being `1003` matches intended behavior.
