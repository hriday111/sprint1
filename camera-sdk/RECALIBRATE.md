# Recalibrate chip marks after moving the lens {#recalibrate}

The intensity sweep does **not** re-detect discs. It averages ellipse masks from `rois.json` (or, for experiments, interpolated ellipses from `rois_interp.json`). After moving the lens (or remounting optics):

```bash
cd camera-sdk
make
mkdir -p tmp

# Single pose (fast)
./build/bin/calibrate 0.1

# Or two poses if blobs drift across the scan window
./build/bin/calibrate2 0.1 400 420
```

Open `tmp/detected.png` (or `tmp/detected_<pos>.png`). Green ellipses must sit on the three dark discs. If not, fix focus/lighting or try another exposure.

Avoid long exposures with a bright laser — clipped discs break detection. Check `logs/calibrate*.log` for saturation and mark-count warnings.

Full API and tool reference: [README.md](README.md).
