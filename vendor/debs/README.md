# Vendored IDS uEye `.deb` packages

Arm64 packages for the IDS Software Suite (uEye API, USB driver, and udev rules). OpenCV is **not** vendored — install it with apt:

```bash
sudo apt-get install -y libopencv-dev
```

## Install uEye + udev

From the repo root:

```bash
sudo dpkg -i vendor/debs/ueye/*.deb
sudo apt-get install -y -f
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo systemctl enable --now ueyeusbdrc
```

`ueye-driver-usb` ships `/lib/udev/rules.d/90-ueyeusbd.rules` (USB vendor `1409` → group `ueye`).

See the project [README prerequisites](../../README.md#prerequisites).
