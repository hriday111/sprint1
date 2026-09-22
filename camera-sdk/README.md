# camera-sdk {#camera_sdk}

Library and tools for the Thorlabs DCC1545M (IDS uEye): capture a fixed mono8 crop, use elliptical disc-mark ROIs, optionally rebuild a two-point interpolation model, and run the interactive intensity sweep. Clients drive the prism motor through FIFOs owned by `servo_calib` (see the [project README](../README.md)).

## Getting started

**Calibration is already complete** for the current optics. Day-to-day use:

1. Start the driver: `sudo ./build/servo_calib` (from the repo root).
2. Run main measurements: `./camera-sdk/build/bin/sweep_run`.

```bash
# Terminal 1 (repo root)
sudo ./build/servo_calib

# Terminal 2
cd camera-sdk
make
./build/bin/sweep_run
```

`sweep_run` presents a TUI: assign diseases D1–D10 to blob 1 and blob 2, then Start/Reset the scan. Quitting leaves `servo_calib` running.

### Recalibration (only if needed)

If a lens move invalidates ROIs and a single `calibrate` / `calibrate2` pass fails, try **several exposures** and keep the best `tmp/detected_*.png`:

```bash
./build/bin/calibrate2 0.20 400 420
./build/bin/calibrate2 0.14 400 420
./build/bin/calibrate2 0.10 400 420
```

## Prerequisites

- IDS uEye SDK (`libueye_api`); `ueyeusbd` running; camera visible as USB vendor `1409`
- OpenCV 4 for `calibrate` / `calibrate2` only
- `servo_calib` running for laser/motion
- Writable `/var/lib/sprint1/`, `./tmp/`, and `logs/`

## What it does

| Module | Responsibility |
|--------|----------------|
| `cam.c` / `cam.h` | Open uEye, set exposure, grab cropped mono8 |
| `roi.c` / `roi.h` | Ellipse JSON blobs, mask means, saturation, PGM |
| `roi_interp.*` | Two-point lerp of ellipse geometry vs servo step |
| `servo_fifo.*` | Shared command/position FIFO client (incl. enable) |
| `detect.cpp` | Find dark discs; emit elliptical blob ROIs |
| `calibrate` | One-shot detect → `rois.json` + overlays |
| `calibrate2` | Detect at pos0 & pos1 → interp model + check CSV |
| `sweep_run` + `sweep_tui` | Interactive disease TUI + intensity sweep → CSV |
| `../logger.c`, `../crash.c` | Shared logging + crash/core-dump hooks |

### Camera crop

| Macro | Value |
|-------|-------|
| `CAM_CROP_X` | 350 |
| `CAM_CROP_Y` | 60 |
| `CAM_CROP_W` | 600 |
| `CAM_CROP_H` | 740 |

Index pixels as `pixels[y * pitch + x]` (full-sensor pitch).

### Camera API

| Function | Returns |
|----------|---------|
| `cam_open` / `cam_close` | `0` / `-1` or void |
| `cam_set_exposure_ms` / `cam_get_exposure_ms` | `0` / `-1` |
| `cam_grab` | `0` / `-1` (cropped view, no copy) |

### ROI / interp / FIFO APIs

| Area | Key symbols |
|------|-------------|
| ROI | `roi_load_json`, `roi_save_json`, `roi_means`, `roi_sat_frac`, `roi_sync_aabb` |
| Interp | `roi_interp_at`, `roi_interp_save`, `roi_interp_load` (`ROI_INTERP_PATH`) |
| FIFO | `servo_fifo_open`, `servo_fifo_send`, `servo_fifo_move_and_wait`, `servo_fifo_enable`, `servo_fifo_close`, `servo_fifo_shutdown` |
| Detect | `detect_boxes_cv`, `frame_mat` |
| TUI | `sweep_tui_run`, `disease_assign_t`, status helpers |

`rois.json` uses `{name,cx,cy,rx,ry,angle}` (legacy `{x,y,w,h}` still loads).

## Tool CLI

| Tool | Args (defaults) | Notes |
|------|-----------------|-------|
| `sweep_run` | `[rois.json] [exposure_ms=0.1] [out.csv=../sensor_data.csv]` | Main measurements; TUI; leaves daemon running |
| `calibrate` | `[exposure_ms=0.1] [n_blobs=3] [roi_path]` | Single-pose ROIs |
| `calibrate2` | `[exposure_ms=0.1] [pos0=400] [pos1=420] [n_blobs=3] …` | Two-point model; try **0.20 / 0.14 / 0.10** if detection fails |

## Detection design

Measurement never re-runs OpenCV mid-sweep. Recalibrate only after optical changes — see [RECALIBRATE.md](RECALIBRATE.md).

## Common pitfalls

| Symptom | Resolution |
|---------|------------|
| `no reader` | Start `./build/servo_calib` first |
| Bad / missing marks | `calibrate2` at 0.20, 0.14, then 0.10; compare overlays |
| Saturation | Lower exposure or laser `bN` |
| `EPIPE` | Keep one command FIFO write fd open |

## Build

```bash
make          # sweep_run, calibrate, calibrate2 → build/bin/
make clean
```

> **Verify on hardware:** crop constants, `/var/lib/sprint1/` paths, uEye exposure API.
