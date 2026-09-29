# Recalibrate chip marks after moving the lens {#recalibrate}

**Day-to-day:** calibration is already done. Start `sudo ./build/src/driver/driver`, then run `sudo ./build/src/camera-sdk/sweep_run`.

The intensity sweep does **not** re-detect discs; it averages ellipse masks from `rois.json`. Recalibrate only after moving the lens (or remounting optics).

```bash
# From the repo root
./sprint1.sh camera-sdk
mkdir -p src/camera-sdk/tmp

# Prefer two-point calibration; try several exposures if detection fails
./build/src/camera-sdk/calibrate2 0.20 400 420
./build/src/camera-sdk/calibrate2 0.14 400 420
./build/src/camera-sdk/calibrate2 0.10 400 420

# Optional single-pose fallback
./build/src/camera-sdk/calibrate 0.1
```

Open `src/camera-sdk/tmp/detected_<pos>.png` (or `tmp/detected.png`). Green ellipses must sit on the three dark discs — pick the exposure that looks best.

Avoid long exposures with a bright laser — clipped discs break detection. Check `logs/calibrate*.log` for saturation and mark-count warnings.

Full camera API: [../README.md](../README.md).
