# camera-sdk {#camera_sdk}

Library and tools for the Thorlabs DCC1545M (IDS uEye): capture a fixed mono8 crop, calibrate disc-mark ROIs with OpenCV, and sweep intensities without OpenCV. Clients drive the prism motor through FIFOs owned by `servo_calib` (see the [project README](../README.md)).

## Prerequisites

- IDS uEye SDK (`libueye_api`); `ueyeusbd` running; camera visible as USB vendor `1409`
- OpenCV 4 (`pkg-config opencv4`) for `calibrate` only
- `servo_calib` running when you need laser control or motion
- Writable `/var/lib/sprint1/` for shared `rois.json` / `saved.prm`, `./tmp/` for diagnostics, and `logs/` in the process cwd

## Quickstart

```bash
cd camera-sdk
make
mkdir -p tmp

# Optional: place ROIs (servo_calib already running for laser)
./build/bin/calibrate 0.1
# Check tmp/detected.png — green ellipses on the three dark discs
# Logs: logs/calibrate-<epoch>.log

# Intensity sweep → CSV
./build/bin/sweep_run /var/lib/sprint1/rois.json 0.1 ../sensor_data.csv
```

Offline ROI parse check (no camera):

```bash
./build/bin/test_roi /var/lib/sprint1/rois.json
```

## What it does

| Module | Responsibility |
|--------|----------------|
| `cam.c` / `cam.h` | Open uEye, set exposure, grab cropped mono8 |
| `roi.c` / `roi.h` | Ellipse JSON blobs, means, saturation fraction, PGM |
| `detect.cpp` | Find dark discs; emit elliptical blob ROIs |
| `calibrate` | One-shot detect → `rois.json` + overlays |
| `sweep_run` | FIFO motion + ROI means → CSV |
| `test_roi` | Load JSON and mean a synthetic frame |
| `../logger.c`, `../crash.c` | Shared logging + crash/core-dump hooks |

Tools call `sprint1_logging_init("<tool>", …)` at startup (DEBUG to stderr and `logs/<tool>-<epoch>.log`). `ERR()` in camera-sdk utils also emits `LOG_FATAL` before exiting.

### Camera crop

Every grab returns a **view** into sensor memory (no copy) clipped to:

| Macro | Value |
|-------|-------|
| `CAM_CROP_X` | 350 |
| `CAM_CROP_Y` | 60 |
| `CAM_CROP_W` | 600 |
| `CAM_CROP_H` | 740 |

ROI coordinates are relative to this crop. Index pixels as `pixels[y * pitch + x]` where `pitch` is the full sensor row stride.

### Camera API

| Function | Parameters | Returns |
|----------|------------|---------|
| `cam_open` | `cam_t *cam` | `0` / `-1` |
| `cam_close` | `cam_t *cam` | void |
| `cam_set_exposure_ms` | `cam`, `ms`, `actual` (nullable) | `0` / `-1` (clamps to sensor range) |
| `cam_get_exposure_ms` | `cam`, `ms` out | `0` / `-1` |
| `cam_grab` | `cam`, `**pixels`, `*width`, `*height`, `*pitch` | `0` / `-1` |

Exposure uses `is_SetExposureTime` (required on UI-154x); auto shutter/gain are disabled at open.

### ROI API

| Function | Role |
|----------|------|
| `roi_load_json` / `roi_save_json` | Ellipse JSON `{name,cx,cy,rx,ry,angle}`; also loads legacy `{x,y,w,h}` as an axis-aligned ellipse |
| `roi_clip` | Clamp AABB to frame; set `pixels` = mask count |
| `roi_means` | Mean intensity 0–255 inside each ellipse mask |
| `roi_sat_frac` | Fraction of pixels ≥ `sat_level` (tools use 250) |
| `roi_draw` / `write_pgm` | Debug overlays / PGM export |
| `detect_boxes_cv` | OpenCV disc finder → up to `ROI_MAX` (8) named `ringN` blobs |
| `frame_mat` | Contiguous `CV_8UC1` Mat from a pitched buffer |

`rois.json` example:

```json
[
  {"name": "ring1", "cx": 173.5, "cy": 147.5, "rx": 47.5, "ry": 45.0, "angle": 12.0},
  {"name": "ring2", "cx": 477.0, "cy": 179.0, "rx": 42.0, "ry": 40.0, "angle": -5.0},
  {"name": "ring3", "cx": 306.5, "cy": 459.5, "rx": 43.0, "ry": 41.0, "angle": 3.0}
]
```

## Tool CLI

| Tool | Args (defaults) | Notes |
|------|-----------------|-------|
| `calibrate` | `[exposure_ms=0.1] [n_blobs=3] [roi_path=/var/lib/sprint1/rois.json]` | Writes `rois.json`, `tmp/detected.png`, `tmp/calib_frame.png` |
| `sweep_run` | `[rois.json] [exposure_ms=0.1] [out.csv=../sensor_data.csv]` | Scans `start`…`stop` from `/var/lib/sprint1/saved.prm`; angle = `pos * 0.1125` |
| `test_roi` | `[rois.json]` | No camera |

**FIFO contract for `sweep_run`:** keep one write fd on `/run/sprint1/data_pipe` for the whole scan; open `pos_pipe` before each `4000`. On exit it sends `-1` (stops the servo daemon).

## Detection design (why calibrate ≠ measure)

Illumination on the chip is uneven, so measurement never re-runs OpenCV. `detect_boxes_cv` flattens the frame, multi-threshold dark blobs, rejects crosses, refines ellipses, and stores the **fitted elliptical blobs** so `roi_means` averages the disc mask (still OpenCV-free during the sweep). Recalibrate after any lens move — see [RECALIBRATE.md](RECALIBRATE.md).

## Common pitfalls

| Symptom | Resolution |
|---------|------------|
| `no uEye camera` | Start `ueyeusbd`; confirm `lsusb` shows `1409` |
| `no reader (is servo_calib running?)` | Start the daemon before FIFO tools |
| Fewer than 3 marks / ellipses off the discs | Lower exposure; check focus; inspect `tmp/calib_frame.png` |
| Saturation warnings | Shorten exposure or reduce laser `bN` |
| `EPIPE` / broken pipe | Do not open/close the command FIFO per step |
| Wrong scan range | Edit `/var/lib/sprint1/saved.prm` (`start stop prep`, 0–999) |
| Undefined `sprint1_logging_init` at link | Build via the Makefile (links `../logger.c` and `../crash.c`) |

## Build

```bash
make          # sweep_run, calibrate, test_roi → build/bin/
make clean
```

Optional helpers not in the default Makefile: `grab_check.c` (camera smoke test), `detect_test.cpp` (offline PNG detection). Both expect the shared logger/crash objects if you compile them by hand.

> **Verify on hardware:** crop constants vs your sensor AOI, default paths under `/var/lib/sprint1/`, and that `is_SetExposureTime` matches your uEye SDK headers.
