# Inx

Reimagined. Improved. Simplified.

Inx is a community firmware for the Xteink X4. It is focused on a cleaner reading experience, better EPUB support, native image rendering, and practical device tools.

*This project is a fork of CrossPoint and is not affiliated with Xteink.*

---

![](./docs/images/cover.jpg)

## What You Can Do

- Read **EPUB**, **XTC / XTCH**, **TXT**, and **MD** files.
- Browse books from **Recent**, **Library**, **Settings**, **Sync**, and **Statistics** tabs.
- Use EPUB features such as bookmarks, annotations, go-to-percent, table of contents, per-book settings, and KOReader sync.
- Render **JPEG**, **PNG**, and **BMP** images directly.
- Use **1-bit** or **2-bit** image rendering.
- Cache rendered images and system data for faster repeat loads.
- Use custom sleep screens, JPEG wallpapers, recent-book sleep screens, or transparent cover sleep screens.
- Connect to Wi-Fi, Calibre, OPDS catalogs, KOReader sync, and the local web file manager.
- Tune reader layout, buttons, fonts, status bar, refresh behavior, and display options.

## Main Features

### Reader

- EPUB paging with saved progress.
- EPUB text annotation and highlight support.
- EPUB bookmarks.
- Go to a specific percentage in an EPUB.
- Table of contents, bookmark, annotation, and footnote navigation from the in-book menu.
- Per-book reader settings.
- Reading statistics.
- KOReader sync support.
- TXT / MD reader.
- XTC / XTCH reader with chapter selection.

### Images

- Native JPEG rendering.
- Native PNG rendering.
- BMP rendering.
- JPEG wallpapers.
- 1-bit and 2-bit image modes.
- Display cache for faster repeated image draws.
- Improved image scaling and dithering.
- Cover and sleep-screen rendering options.

### Library

- Recent books page.
- Folder-based library browser.
- Flat all-books view.
- Favorites.
- Sort options.
- Optional indexed library mode for faster browsing.

### Display

- Text anti-aliasing.
- Configurable refresh frequency.
- Sunlight fade fix.
- Sleep screen modes:
  - Dark
  - Light
  - Custom image
  - Recent book
  - Transparent cover
  - None
- Custom sleep images from `/sleep/` or `/sleep.bmp`.

### Sync & Network

- Join Wi-Fi networks.
- Create a hotspot.
- Connect to Calibre.
- Browse OPDS catalogs.
- Use KOReader sync.
- Upload files through the local web interface.

### Settings

Settings are split into simple **System** and **Reader** panels.

System settings include:

- Sleep screen.
- Sleep image picker.
- Recent page mode.
- Button layout.
- Power button behavior.
- Time to sleep.
- Library indexing.
- Cache clearing.
- KOReader, OPDS, Calibre, and OTA update tools.
- About page with device memory information.

Reader settings include:

- Font family and size.
- Line spacing.
- Screen margins.
- Paragraph alignment.
- Reading orientation.
- Hyphenation.
- Page navigation mapping.
- Auto page turn.
- Text anti-aliasing.
- Image grayscale / 2-bit rendering.
- Status bar layout.

## Custom Sleep Images

Put sleep images on the SD card:

```text
/sleep/
  image1.bmp
  image2.jpg
  image3.png

/sleep.bmp
```

You can choose a fixed sleep image from settings, or let the device pick one randomly.

## Cache

Inx uses SD-card cache files to save RAM and speed up repeated work.

Main cache locations:

```text
/.metadata/       EPUB metadata, layout, progress, stats, annotations
/.system/cache/   Display and image cache
/.system/         Settings and system data
```

You can clear cache from **Settings → Actions → Clear Cache**.

Deleting `/.metadata` will force EPUB layout data to be rebuilt.

## Installing

### Web Flash

1. Connect your Xteink X4 to your computer with USB-C.
2. Download `firmware.bin` from the [releases page](https://github.com/obijuankenobiii/inx/releases).
3. Open [xteink.dve.al](https://xteink.dve.al/).
4. Flash the firmware using the OTA fast flash controls.

To return to the official firmware, flash the latest official firmware from [xteink.dve.al](https://xteink.dve.al/) or use the debug page to swap boot partitions.

## Development

### Requirements

- PlatformIO Core (`pio`) or VS Code with PlatformIO.
- Python 3.8 or newer.
- USB-C cable.
- Xteink X4.

### Clone

```sh
git clone --recursive https://github.com/obijuankenobiii/inx
cd inx
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

### Build

```sh
pio run
```

### Flash

```sh
pio run --target upload
```

### Serial Debugging

Install the monitor dependencies:

```sh
python3 -m pip install pyserial colorama matplotlib
```

Run the monitor:

```sh
# Linux
python3 scripts/debugging_monitor.py

# macOS example
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

## Contributing

Contributions are welcome.

1. Fork the repository.
2. Create a branch.
3. Make your changes.
4. Open a pull request.

