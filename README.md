# Sprint1 — Prism scan controller {#mainpage}

Sprint1 runs a Raspberry Pi–mounted A4988 stepper, laser, and endstop (“the black box”) while `camera-sdk/` grabs frames from a Thorlabs DCC1545M (IDS uEye) and records ROI intensities. The two processes talk over named FIFOs under `/run/sprint1/`. This guide is for engineers operating the rig on a Pi 4 with WiringPi and the uEye SDK.

## Prerequisites

- Raspberry Pi 4 with WiringPi (`-lwiringPi`)
- Root (or equivalent GPIO) privileges for `servo_calib` (hardware PWM on BCM 18)
- IDS uEye SDK (`libueye_api`); OpenCV 4 for `calibrate` only
- Hardware wired per the pin table below
- Writable `/run/sprint1/` (FIFOs), `/var/lib/sprint1/` (`rois.json`, `saved.prm`), and a local `logs/` directory (created on start)
- A sample `saved.prm` lives in the repo; `rois.json` is produced by `calibrate`

## Quickstart

```bash
# Terminal 1 — motor + laser daemon
sudo make
sudo ./build/servo_calib
# Logs: logs/servo_calib-<epoch>.log (+ stderr)

# Terminal 2 — recalibrate ROIs after moving the lens (optional)
cd camera-sdk && make
mkdir -p tmp
./build/bin/calibrate 0.1
# Inspect tmp/detected.png — green boxes must sit inside the dark discs

# Terminal 2 — intensity sweep (servo_calib still running)
./build/bin/sweep_run /var/lib/sprint1/rois.json 0.1 ../sensor_data.csv
```

On success, `sensor_data.csv` has one row per step: `Angle_deg` plus one mean column per ROI.

**Note:** When `sweep_run` finishes it sends `-1` on the command FIFO, which **exits** `servo_calib`. Restart the daemon before another sweep.

## Architecture

| Component | Role |
|-----------|------|
| `build/servo_calib` | GPIO, home on endstop, serve commands, publish position, logging/crash hooks |
| `camera-sdk/sweep_run` | Move start→stop, grab crop, average ROIs, write CSV (no OpenCV) |
| `camera-sdk/calibrate` | Detect discs once; write boxes to `rois.json` |
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

**`/var/lib/sprint1/rois.json`** — up to 8 boxes in crop coordinates (`CAM_CROP_*`: 600×740 at origin (350, 60) on the sensor). Intensity is the rectangle mean (no ellipse mask at scan time).

## Common pitfalls

| Symptom | Fix |
|---------|-----|
| `no reader (is servo_calib running?)` | Start `sudo ./build/servo_calib` first |
| `EPIPE` mid-sweep | Keep one `O_WRONLY` fd open for the whole scan |
| `position: no reply after retries` | Open `pos_pipe` for read **before** sending `4000` |
| Boxes miss the discs | Run `./calibrate` after any lens move |
| ROI saturation warning | Lower exposure or `b` brightness |
| `loadParams: unsupported values` | Keep start/stop/prep in 0–999 |
| Goto fails after boot | Confirm endstop on GPIO 17; enable before absolute moves |
| Daemon gone after sweep | Expected: `sweep_run` sends `-1` on exit |
| No `logs/` file | Process must start in a writable cwd (creates `logs/`) |

## Why fixed ROIs?

Detection runs only at calibration time. Measurement is a cheap mean over boxes so `sweep_run` stays OpenCV-free and keeps up with step-by-step motion. After any optical change, recalibrate once. See [camera-sdk/README.md](camera-sdk/README.md) for the full camera API and tool reference.

API HTML (Doxygen): run `./docs/generate.sh`, then open `docs/html/index.html`.

> **Verify on hardware:** uEye device permissions, whether `/var/lib/sprint1/` is provisioned on your image, and that sharing value `1003` for disable/reset matches intended behavior.
