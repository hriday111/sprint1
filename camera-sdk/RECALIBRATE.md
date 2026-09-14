# Recalibrate chip marks after moving the lens {#recalibrate}

The intensity sweep does **not** re-detect discs. It averages fixed elliptical blob masks from `rois.json`. After moving the lens (or remounting optics):

```bash
cd camera-sdk
make
mkdir -p tmp
./build/bin/calibrate 0.1    # servo_calib already running (laser)
```

Open `tmp/detected.png`. Green ellipses must sit on the three dark discs. If not, fix focus/lighting or try another exposure (e.g. `./build/bin/calibrate 0.14`).

Avoid long exposures with a bright laser — clipped discs break detection. Check `logs/calibrate-*.log` for saturation and mark-count warnings.

Full API and tool reference: [README.md](README.md).
