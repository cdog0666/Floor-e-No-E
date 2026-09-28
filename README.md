# Balloon Game

Static BLE balloon game for the Seeed Studio XIAO nRF52840 Sense.

## BLE protocol

The website connects directly to the XIAO and listens for:

- `READY`
- `GROUND_HIT`
- `RESET`

BLE service:

`19B10000-E8F2-537E-4F6C-D104768A1214`

BLE characteristic:

`19B10001-E8F2-537E-4F6C-D104768A1214`

## GitHub Pages

1. Create a GitHub repository.
2. Upload `index.html`.
3. Go to Settings → Pages.
4. Select Deploy from branch.
5. Select the branch containing `index.html`.
6. Open the generated HTTPS GitHub Pages URL in Chrome or another browser that supports Web Bluetooth.

The website connects directly to the XIAO. Python is not required.

## Important

Web Bluetooth requires a supported browser and a secure context. GitHub Pages provides HTTPS, so it is suitable for hosting this site.
