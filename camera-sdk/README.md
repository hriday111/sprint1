# camera-sdk {#camera_sdk}

Library and tools for the Thorlabs DCC1545M (IDS uEye): capture a fixed mono8 crop, calibrate elliptical disc-mark ROIs with OpenCV, optionally build a two-point interpolation model, and sweep intensities without OpenCV. Clients drive the prism motor through FIFOs owned by `servo_calib` (see the [project README](../README.md)).

## Prerequisites

- IDS uEye SDK (`libueye_api`); `ueyeusbd` running; camera visible as USB vendor `1409`
- OpenCV 4 (`pkg-config opencv4`) for `calibrate` and `calibrate2`
- `servo_calib` running when you need laser control or motion
- Writable `/var/lib/sprint1/` for shared ROI/prm files, `./tmp/` for diagnostics, and `logs/` in the process cwd

## Quickstart

```bash
cd camera-sdk
make
mkdir -p tmp

# Single-pose ROIs (servo_calib running for laser)
./build/bin/calibrate 0.1
# Check tmp/detected.png — green ellipses on the three dark discs

# Two-point interp + intensity check (daemon left running)
./build/bin/calibrate2 0.1 400 420
# → /var/lib/sprint1/rois_interp.json, tmp/interp_intensity.csv,
#   tmp/detected_400.png, tmp/detected_420.png

# Intensity sweep
./build/bin/sweep_run /var/lib/sprint1/rois.json 0.1 ../sensor_data.csv
```

## What it does

| Module | Responsibility |
|--------|----------------|
| `cam.c` / `cam.h` | Open uEye, set exposure, grab cropped mono8 |
| `roi.c` / `roi.h` | Ellipse JSON blobs, mask means, saturation, PGM |
| `roi_interp.*` | Two-point lerp of ellipse geometry vs servo step |
| `servo_fifo.*` | Shared command/position FIFO client |
| `detect.cpp` | Find dark discs; emit elliptical blob ROIs |
| `calibrate` | One-shot detect → `rois.json` + overlays |
| `calibrate2` | Detect at pos0 & pos1 → interp model + check CSV |
| `sweep_run` | FIFO motion + ROI means → CSV (sends `-1` on exit) |
| `../logger.c`, `../crash.c` | Shared logging + crash/core-dump hooks |

Tools call `sprint1_logging_init("<tool>", …)` at startup. `ERR()` emits `LOG_FATAL` before exiting.

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
| `cam_set_exposure_ms` | `cam`, `ms`, `actual` (nullable) | `0` / `-1` |
| `cam_get_exposure_ms` | `cam`, `ms` out | `0` / `-1` |
| `cam_grab` | `cam`, `**pixels`, `*width`, `*height`, `*pitch` | `0` / `-1` |

Exposure uses `is_SetExposureTime` (required on UI-154x); auto shutter/gain are disabled at open.

### ROI API

| Function | Role |
|----------|------|
| `roi_load_json` / `roi_save_json` | Ellipse JSON `{name,cx,cy,rx,ry,angle}`; legacy `{x,y,w,h}` → axis-aligned ellipse |
| `roi_parse_json` | Same parse from an in-memory string |
| `roi_sync_aabb` | Refresh AABB `x,y,w,h` from ellipse fields |
| `roi_clip` | Clamp AABB; recount mask pixels |
| `roi_means` / `roi_sat_frac` | Stats inside the ellipse mask |
| `roi_draw` / `write_pgm` | Debug overlays / PGM export |
| `detect_boxes_cv` | OpenCV disc finder → up to `ROI_MAX` (8) named `ringN` |
| `frame_mat` | Contiguous `CV_8UC1` Mat from a pitched buffer |

### Interpolation API (`roi_interp`)

| Function | Role |
|----------|------|
| `roi_interp_at` | Lerp `cx,cy,rx,ry,angle` for step `pos` (extrapolates outside endpoints) |
| `roi_interp_save` / `roi_interp_load` | JSON model at `ROI_INTERP_PATH` (`/var/lib/sprint1/rois_interp.json`) |

### Servo FIFO API (`servo_fifo`)

| Function | Role |
|----------|------|
| `servo_fifo_open` / `servo_fifo_send` | Keep one write fd; send integer commands |
| `servo_fifo_position` / `servo_fifo_move_and_wait` | GETPOS (`4000`) + poll until target |
| `servo_fifo_close` | Close **without** `-1` (daemon keeps running) |
| `servo_fifo_shutdown` | Send `-1` then close (stops daemon) |

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
| `calibrate` | `[exposure_ms=0.1] [n_blobs=3] [roi_path]` | Writes `rois.json`, `tmp/detected.png`, `tmp/calib_frame.png` |
| `calibrate2` | `[exposure_ms=0.1] [pos0=400] [pos1=420] [n_blobs=3] [interp_path] [csv_path]` | Detects at both poses; writes interp JSON + `tmp/interp_intensity.csv`; leaves daemon running |
| `sweep_run` | `[rois.json] [exposure_ms=0.1] [out.csv=../sensor_data.csv]` | Scans `saved.prm` start…stop; angle = `pos * 0.1125`; **exits** daemon |

**Check CSV columns (`calibrate2`):** `pos`, `<name>_interp`, `<name>_fixed`, `<name>_cx`, `<name>_cy`. Flat `_interp` intensity vs pos suggests the lerp tracks the discs; rising `_fixed` drift shows why a single pose is not enough.

> **Note:** `calibrate2` may print a hint to run `plot_interp_check.py` — that helper is illustrative and may not be in the tree; plot the CSV with any tool you prefer.

## Detection design

Illumination on the chip is uneven, so measurement never re-runs OpenCV mid-sweep. `detect_boxes_cv` flattens the frame, multi-threshold dark blobs, rejects crosses, refines ellipses, and stores fitted elliptical blobs for `roi_means`. Recalibrate after any lens move — see [RECALIBRATE.md](RECALIBRATE.md).

## Common pitfalls

| Symptom | Resolution |
|---------|------------|
| `no uEye camera` | Start `ueyeusbd`; confirm `lsusb` shows `1409` |
| `no reader (is servo_calib running?)` | Start the daemon before FIFO tools |
| Fewer than 3 marks / ellipses off the discs | Lower exposure; check focus; inspect `tmp/calib_frame_*.png` |
| Saturation warnings | Shorten exposure or reduce laser `bN` |
| `EPIPE` / broken pipe | Do not open/close the command FIFO per step |
| Wrong scan range | Edit `/var/lib/sprint1/saved.prm` (`start stop prep`, 0–999) |
| Daemon dies after calibrate2 | Should not — use `servo_fifo_close`; if you called shutdown, restart servo |
| Undefined `sprint1_logging_init` at link | Build via the Makefile (links logger + crash) |

## Build

```bash
make          # sweep_run, calibrate, calibrate2 → build/bin/
make clean
```

Optional (not default targets): `grab_check.c`, `detect_test.cpp`.

> **Verify on hardware:** crop constants vs sensor AOI, `/var/lib/sprint1/` paths, and `is_SetExposureTime` vs your uEye SDK headers.
