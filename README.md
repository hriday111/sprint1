# Sprint1 — Prism scan controller {#mainpage}

Sprint1 drives a Raspberry Pi–mounted motor and illumination (“the black box”) while `camera-sdk/` captures frames from a Thorlabs DCC1545M (IDS uEye) and records ROI intensities. The **device driver** and **camera tools** talk over named FIFOs under `/run/sprint1/`.

Hardware backends are selected at build time (`MOTOR`, `ILLUMINATION`, `CAMERA`). Default build: A4988 stepper + laser PWM + uEye camera.

## Documentation

- Published API: [https://hriday111.github.io/sprint1](https://hriday111.github.io/sprint1)
- Architecture source: [docs/architecture.puml](docs/architecture.puml) (SVGs rendered by the Docs workflow onto GitHub Pages)
- Local Doxygen: `doxygen Doxyfile` → `docs/html/index.html`

## Getting started (normal run)

**ROI calibration is already done** for the current optics (`/var/lib/sprint1/rois.json`). Day-to-day you can use `sprint1.sh` to build and/or run:

```bash
./sprint1.sh build      # make all (driver + camera-sdk)
./sprint1.sh driver     # build driver only
./sprint1.sh camera-sdk # build camera-sdk only
./sprint1.sh rebuild    # make clean && make all
./sprint1.sh run        # start driver, then sweep_run (stops driver on exit)
```

Or run the two processes yourself:

```bash
# Terminal 1 — device driver (motor + illumination + FIFO server)
sudo make driver          # or: sudo make   (also builds camera-sdk)
sudo ./build/driver

# Terminal 2 — main intensity measurements (interactive TUI)
cd camera-sdk && make
./build/bin/sweep_run
```

`sweep_run` assigns diseases D1–D10 to blob 1 and blob 2, then Start/Reset for the scan. Quitting leaves `./build/driver` running (FIFO closed without `-1`) unless you used `./sprint1.sh run`, which sends `-1` on exit.

### If you must recalibrate

Only after moving the lens or remounting optics. Prefer `calibrate2` and try several exposures:

```bash
cd camera-sdk && mkdir -p tmp
./build/bin/calibrate2 0.20 400 420
./build/bin/calibrate2 0.14 400 420
./build/bin/calibrate2 0.10 400 420
```

Compare `tmp/detected_*.png` — green ellipses must sit on the three dark discs. See [camera-sdk/app/RECALIBRATE.md](camera-sdk/app/RECALIBRATE.md).

## Prerequisites

Hardware: Raspberry Pi 4, WiringPi, root (or GPIO) for the driver (PWM on BCM 18). Shared paths: `/run/sprint1/`, `/var/lib/sprint1/`, and a writable `logs/` cwd.

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
| `build/driver` | Device façade: home, park, serve FIFO commands |
| `device.c` | Composes `motor_api` + `illumination_api` + `ipc_server` |
| `A4988/` / `SG-90/` | Motor backends (`make MOTOR=a4988\|sg90`) |
| `Laser/` / `Led/` | Illumination backends (`make ILLUMINATION=laser\|led`) |
| `ipc/` | FIFO server (driver) and client (camera tools) |
| `camera-sdk/app/` | `sweep_run`, `calibrate`, `calibrate2`, TUI |
| `camera-sdk/vision/` | ROI, detect, interp |
| `camera-sdk/thorlabs-dcc1545m/` | uEye backend (`CAMERA=ueye`) |
| `logging/` | File + console logger, crash/core-dump hooks |

| Path | Direction | Purpose |
|------|-----------|---------|
| `/run/sprint1/data_pipe` | client → driver | newline-terminated commands |
| `/run/sprint1/pos_pipe` | driver → client | position after `4000` |

### Build knobs

```bash
make driver                         # default: MOTOR=a4988 ILLUMINATION=laser
make driver MOTOR=sg90 ILLUMINATION=led
make camera                         # camera-sdk tools
make                                # driver + camera
```

## Pinout

BCM pins in `gpio_pins.h`. Enable is active LOW; endstop is active HIGH; illumination uses hardware PWM on GPIO 18.

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
| `no reader` | Start `sudo ./build/driver` first |
| `EPIPE` | Keep one write fd open for the whole session |
| Bad ellipses | `calibrate2` at 0.20 / 0.14 / 0.10 |
| Wrong binary name | Driver is `build/driver` (not `servo_calib`) |

Camera tools detail: [camera-sdk/README.md](camera-sdk/README.md).
