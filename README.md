# Sprint1 — Prism scan controller {#mainpage}

Sprint1 drives a Raspberry Pi–mounted motor and illumination (“the black box”) while `src/camera-sdk/` captures frames from a Thorlabs DCC1545M (IDS uEye) and records ROI intensities. The **device driver** and **camera tools** talk over named FIFOs under `/run/sprint1/`.

Hardware backends are selected at CMake configure time (`MOTOR`, `ILLUMINATION`). Default build: A4988 stepper + laser PWM + uEye camera.

## Documentation

- Published API: [https://hriday111.github.io/sprint1](https://hriday111.github.io/sprint1)
- Architecture source: [docs/architecture.puml](docs/architecture.puml) (SVGs rendered by the Docs workflow onto GitHub Pages)
- Local Doxygen: `doxygen Doxyfile` → `docs/html/index.html`

## Repository layout

```
sprint1/
├── CMakeLists.txt          # top-level: add_subdirectory(src)
├── sprint1.sh              # build / run helper
├── docs/                   # architecture.puml + generated HTML/SVG
├── vendor/                 # uEye debs, etc.
└── src/
    ├── CMakeLists.txt
    ├── driver/             # device façade + motor/illumination backends
    ├── camera-sdk/         # sweep_run, calibrate, calibrate2
    ├── ipc/                # FIFO server (driver) + client (camera)
    ├── logging/
    ├── common/
    └── TUI/
```

Binaries land under `build/src/` after `cmake -S . -B build`:

| Binary | Path |
|--------|------|
| Driver | `build/src/driver/driver` |
| Sweep | `build/src/camera-sdk/sweep_run` |
| Calibrate | `build/src/camera-sdk/calibrate` |
| Calibrate2 | `build/src/camera-sdk/calibrate2` |

## Getting started (normal run)

**ROI calibration is already done** for the current optics (`/var/lib/sprint1/rois.json`). Day-to-day you can use `sprint1.sh` to build and/or run:

```bash
./sprint1.sh build      # cmake configure + build (driver + camera-sdk)
./sprint1.sh driver     # build driver only
./sprint1.sh camera-sdk # build camera tools only
./sprint1.sh rebuild    # wipe build/, reconfigure, build all
./sprint1.sh run        # start driver, then sweep_run (stops driver on exit)
```

Or run the two processes yourself:

```bash
# Configure + build once
cmake -S . -B build
cmake --build build

# Terminal 1 — device driver (motor + illumination + FIFO server)
sudo ./build/src/driver/driver

# Terminal 2 — main intensity measurements (interactive TUI)
sudo ./build/src/camera-sdk/sweep_run
```

`sweep_run` assigns diseases D1–D10 to blob 1 and blob 2, then Start/Reset for the scan. Quitting leaves the driver running (FIFO closed without `-1`) unless you used `./sprint1.sh run`, which sends `-1` on exit.

### If you must recalibrate

Only after moving the lens or remounting optics. Prefer `calibrate2` and try several exposures:

```bash
mkdir -p src/camera-sdk/tmp
./build/src/camera-sdk/calibrate2 0.20 400 420
./build/src/camera-sdk/calibrate2 0.14 400 420
./build/src/camera-sdk/calibrate2 0.10 400 420
```

Compare `src/camera-sdk/tmp/detected_*.png` — green ellipses must sit on the three dark discs. See [src/camera-sdk/app/RECALIBRATE.md](src/camera-sdk/app/RECALIBRATE.md).

## Prerequisites

Hardware: Raspberry Pi 4, WiringPi, root (or GPIO) for the driver (PWM on BCM 18). Shared paths: `/run/sprint1/`, `/var/lib/sprint1/`, and a writable `logs/` cwd. Build requires **CMake ≥ 3.10**.

### Library installation

**OpenCV** (for `calibrate` / `calibrate2`):

```bash
sudo apt-get install -y libopencv-dev
```

**IDS uEye + udev** (vendored under `vendor/debs/ueye/`):

```bash
sudo dpkg -i vendor/debs/ueye/*.deb
sudo apt-get install -y -f
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo systemctl enable --now ueyeusbdrc
```

**WiringPi** (from upstream if needed):

```bash
sudo apt install git
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi && ./build debian
sudo apt install ./debian-template/wiringpi-3.x.deb
```

| Dependency | Source |
|------------|--------|
| **CMake ≥ 3.10** | `apt-get install -y cmake` |
| **OpenCV 4** | `apt-get install -y libopencv-dev` |
| **IDS uEye + udev** | `vendor/debs/ueye/*.deb` |
| **WiringPi** | [WiringPi](https://github.com/WiringPi/WiringPi) (`-lwiringPi`) |

## Architecture

![Sprint1 components and FIFO IPC](https://hriday111.github.io/sprint1/architecture.svg)

<details>
<summary>Sweep sequence and build variants</summary>

![Sweep measurement sequence](https://hriday111.github.io/sprint1/sweep_sequence.svg)

![Build-time backend selection](https://hriday111.github.io/sprint1/build_variants.svg)

</details>

Source: [docs/architecture.puml](docs/architecture.puml). Edit the `.puml` and push — the Docs workflow re-renders and publishes the SVGs; nothing commits back to `main`.

| Component | Role |
|-----------|------|
| `build/src/driver/driver` | Device façade: home, park, serve FIFO commands |
| `src/driver/device.c` | Composes `motor_api` + `illumination_api` + `ipc_server` |
| `src/driver/A4988/` / `SG-90/` | Motor backends (`-DMOTOR=a4988\|sg90`) |
| `src/driver/Laser/` / `Led/` | Illumination backends (`-DILLUMINATION=laser\|led`) |
| `src/ipc/` | FIFO server (driver) and client (camera tools) |
| `src/camera-sdk/app/` | `sweep_run`, `calibrate`, `calibrate2`, TUI |
| `src/camera-sdk/vision/` | ROI, detect, interp |
| `src/camera-sdk/thorlabs-dcc1545m/` | uEye backend |
| `src/logging/` | File + console logger, crash/core-dump hooks |

| Path | Direction | Purpose |
|------|-----------|---------|
| `/run/sprint1/data_pipe` | client → driver | newline-terminated commands |
| `/run/sprint1/pos_pipe` | driver → client | position after `4000` |

### Build knobs

```bash
./sprint1.sh build                                    # defaults: MOTOR=a4988 ILLUMINATION=laser
cmake -S . -B build -DMOTOR=sg90 -DILLUMINATION=led   # alternate backends
cmake --build build --target driver                   # driver only
cmake --build build --target sweep_run                # one camera tool
```

## Pinout

BCM pins in `src/driver/gpio_pins.h`. Enable is active LOW; endstop is active HIGH; illumination uses hardware PWM on GPIO 18.

| Signal | GPIO | Typical wire |
|--------|------|--------------|
| Enable | 20 | Green |
| MS1 / MS2 / MS3 | 13 / 19 / 26 | Blue / Purple / Grey |
| Step / Dir | 27 / 22 | White / Black |
| Illumination / Endstop | 18 / 17 | Blue→White / Grey |

Microstepping (A4988): 1/16. Step half-period: `STEP_DELAY_US` (20 ms).

## Command protocol

ASCII lines on `data_pipe`. Integer lines are commands; `bN` sets illumination brightness (0–255).

| Input | Meaning |
|-------|---------|
| `0`–`1000` | Absolute goto step |
| `1001` / `1002` | Illumination on / off |
| `1003` | Disable motor |
| `1004` | Enable motor |
| `1006` / `1007` | Single step forward / back |
| `2000` | Reload `/var/lib/sprint1/saved.prm` |
| `4000` | Publish position on `pos_pipe` |
| `-1` | Exit driver |

```bash
exec 3>/run/sprint1/data_pipe
printf 'b255\n1001\n4000\n' >&3
timeout 1 cat /run/sprint1/pos_pipe
```

Angle: **0.1125°/step**. Homing ≈ `CMD_POS_ZERO` + `DELTA` (400 + 34).

## Config files

- **`/var/lib/sprint1/saved.prm`** — `start stop prep` (each 0–999)
- **`/var/lib/sprint1/rois.json`** — elliptical blobs `{name,cx,cy,rx,ry,angle}`
- **`/var/lib/sprint1/rois_interp.json`** — two-point model from `calibrate2`

## Logging

`sprint1_logging_init` writes `logs/<app>-<epoch>.log` + stderr (DEBUG, 100 ms auto-flush). Fatal signals log the expected core path then re-raise.

## Common pitfalls

| Symptom | Fix |
|---------|-----|
| `no reader` | Start `sudo ./build/src/driver/driver` first |
| `EPIPE` | Keep one write fd open for the whole session |
| Bad ellipses | `calibrate2` at 0.20 / 0.14 / 0.10 |
| Wrong binary path | Driver is `build/src/driver/driver` (not `build/driver` / `servo_calib`) |

Camera tools detail: [src/camera-sdk/README.md](src/camera-sdk/README.md).
