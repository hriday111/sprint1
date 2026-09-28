# camera-sdk {#camera_sdk}

Thorlabs DCC1545M (IDS uEye) tools: mono8 crop, elliptical ROIs, optional two-point interpolation, and an interactive intensity sweep. Motion/illumination go through FIFOs owned by `./build/driver` (see [project README](../README.md)).

## Getting started

**Calibration is already done.** Day-to-day:

```bash
# Terminal 1 (repo root)
sudo ./build/driver

# Terminal 2
cd camera-sdk && make
./build/bin/sweep_run
```

### Recalibration (only if needed)

```bash
./build/bin/calibrate2 0.20 400 420
./build/bin/calibrate2 0.14 400 420
./build/bin/calibrate2 0.10 400 420
```

See [app/RECALIBRATE.md](app/RECALIBRATE.md).

## Layout

| Path | Role |
|------|------|
| `app/` | `sweep_run`, `sweep_tui`, `calibrate`, `calibrate2` |
| `vision/` | `roi`, `roi_interp`, `detect` |
| `thorlabs-dcc1545m/` | uEye backend implementing `cam_api.h` |
| `../ipc/ipc_client.*` | FIFO client (`fifo_*`) |
| `../logging/` | Shared logger / crash hooks |

## Prerequisites

```bash
sudo apt-get install -y libopencv-dev
sudo dpkg -i ../vendor/debs/ueye/*.deb && sudo apt-get install -y -f
sudo systemctl enable --now ueyeusbdrc
```

Driver must be running for laser/motion. Full install notes: [../README.md](../README.md#prerequisites).

## APIs (short)

| Area | Entry points |
|------|----------------|
| Camera | `camera_api.open/close/set_exposure_ms/grab` (`cam_api.h`) |
| ROI | `roi_load_json`, `roi_means`, `roi_sat_frac` |
| Interp | `roi_interp_at/save/load` |
| FIFO | `fifo_open`, `fifo_send`, `fifo_move_and_wait`, `fifo_illum_on`, `fifo_close` |
| Detect | `detect_boxes_cv`, `frame_mat` |

Crop (sensor coords): `CAM_CROP_*` = 600×740 at (350, 60).

## Tool CLI

| Tool | Notes |
|------|-------|
| `sweep_run` | Main measurements; disease TUI; leaves driver running |
| `calibrate` | Single-pose → `rois.json` |
| `calibrate2` | Two poses → `rois_interp.json`; try exposures **0.20 / 0.14 / 0.10** |

## Build

```bash
make                          # from repo root: make camera
# or: make CAMERA=ueye
```

Architecture diagrams: [../docs/architecture.puml](../docs/architecture.puml).
