# Inx

Reimagined. Improved. Simplified.

Inx is a community firmware for Xteink e-paper readers. It is focused on a cleaner reading experience, better EPUB support, native image rendering, SD-card fonts, and practical device tools.

*This project is a fork of CrossPoint and is not affiliated with Xteink.*

---

![](./docs/images/cover.jpg)

## What You Can Do

- Read **EPUB**, **XTC / XTCH**, **TXT**, and **MD** files.
- Browse books from **Recent**, **Library**, **Settings**, **File Transfer**, and **Statistics** tabs.
- Use EPUB features such as bookmarks, annotations, go-to-percent, table of contents, footnotes, per-book settings, and KOReader sync.
- Render **JPEG**, **PNG**, and **BMP** images directly.
- Use **1-bit** or **2-bit** image rendering.
- Cache rendered images and system data for faster repeat loads.
- Use custom sleep screens, recent-book sleep screens, transparent cover sleep screens, or date/time sleep screens on supported devices.
- Install reader fonts from the SD card instead of baking large fonts into firmware.
- Connect to Wi-Fi, Calibre, OPDS catalogs, KOReader sync, and the local web file manager.
- Tune reader layout, buttons, fonts, status bar, refresh behavior, image quality, and display options.

## Main Features

### Reader

- EPUB paging with saved progress.
- EPUB layout support for tables, drop caps, borders, images, lists, blockquotes, superscript/subscript, and common CSS spacing/alignment.
- EPUB text annotation and highlight support.
- EPUB bookmarks.
- Go to a specific percentage in an EPUB.
- Table of contents, bookmark, annotation, and footnote navigation from the in-book menu.
- EPUB menu tools for deleting cache/progress, deleting a book, generating full data, and regenerating thumbnails.
- Per-book reader settings and reader presets.
- Reading statistics.
- KOReader sync support.
- TXT / MD reader.
- XTC / XTCH reader with chapter selection.
- Auto page turn support for EPUB and XTC reading.

### Images

- Native JPEG rendering.
- Native PNG rendering.
- BMP rendering.
- 1-bit and 2-bit image modes.
- Low, medium, and high image quality options for reader images.
- Low, medium, and high sleep image quality options.
- Display cache for faster repeated image draws.
- Improved image scaling and dithering.
- Cover, thumbnail, and sleep-screen rendering options.
- Thumbnail generation for EPUB and XTC books.

### Library

- Recent books page.
- Folder-based library browser.
- Flat all-books view.
- Cover shelf view for EPUB and XTC books.
- Tag view when the library index is enabled.
- Favorites.
- Sort options by title, group/folder, reading state, and tag.
- Optional indexed library mode for faster browsing and tag management.
- List and grid library modes.

### Display

- Text anti-aliasing.
- Configurable refresh frequency.
- Optional half refresh when opening main tabs.
- Sleep screen modes:
  - Dark
  - Light
  - Custom image
  - Recent book
  - Transparent cover
  - None
  - Date/time on supported devices
- Custom sleep images from `/sleep/`, `/sleep.bmp`, `/sleep.jpg`, or `/sleep.jpeg`.

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
- Library mode.
- Button layout.
- Power button behavior.
- Time to sleep.
- Library indexing.
- Library custom sort.
- Cache clearing.
- Thumbnail generation.
- KOReader, OPDS, Calibre, and OTA update tools.
- About page with device memory information.

Reader settings include:

- Font family and size.
- SD-card font families.
- Line height and word spacing.
- Screen margins.
- Paragraph alignment.
- CSS indentation.
- Reading orientation.
- Hyphenation.
- Bionic Reading.
- Page navigation mapping.
- Long-press chapter or page skipping.
- Auto page turn.
- Text anti-aliasing.
- Image grayscale / 2-bit rendering.
- Smart refresh on image-heavy pages.
- Status bar layout.

## Web Interface

The local web interface includes:

- **Dashboard**: device status, IP address, Wi-Fi strength, memory, uptime, and quick links.
- **Files**: browse folders, upload files, create folders, delete files, upload cover art to `/sleep`, and set folder thumbnails.
- **Epub**: drag-and-drop EPUB imports, folder creation, JPEG optimization, optional packaged device thumbnails, and import progress.
- **Tags**: create reusable tags and assign them to indexed books.
- **Fonts**: build SD-card font packs from TTF/OTF files and upload them to `/fonts`.
- **Settings**: edit system settings, reader settings, Wi-Fi networks, KOReader settings, and OPDS servers.

## Fonts

Inx includes built-in **Literata** and **Atkinson Hyperlegible** reader fonts.

You can also install fonts on the SD card:

```text
/fonts/
  MyFont/
    Regular_10.bin
    Regular_12.bin
    Regular_14.bin
    Bold_14.bin
    Italic_14.bin
    BoldItalic_14.bin
```

The web font manager converts TTF/OTF files into the `.bin` format used by the reader. Regular is required; bold, italic, and bold italic are optional.

## Custom Sleep Images

Put sleep images on the SD card:

```text
/sleep/
  image1.bmp
  image2.jpg
  image3.png

/sleep.bmp
/sleep.jpg
/sleep.jpeg
```

You can choose a fixed sleep image from settings, or let the device pick one randomly.

## Cache

Inx uses SD-card cache files to save RAM and speed up repeated work.

Main cache locations:

```text
/.metadata/       EPUB metadata, layout, progress, stats, annotations
/.metadata/xtc/   XTC / XTCH metadata and progress
/.system/cache/   Display and image cache
/.system/         Settings, TXT cache, and system data
/fonts/           SD-card reader fonts
/sleep/           Custom sleep images
```

You can clear cache from **Settings -> Actions -> Delete Cache**.

Deleting `/.metadata` will force EPUB layout data to be rebuilt.

## Installing

### Web Flash

1. Connect your Xteink device to your computer with USB-C.
2. Download `firmware.bin` from the [releases page](https://github.com/obijuankenobiii/inx/releases).
3. Open [xteink.dve.al](https://xteink.dve.al/).
4. Flash the firmware using the OTA fast flash controls.

To return to the official firmware, flash the latest official firmware from [xteink.dve.al](https://xteink.dve.al/) or use the debug page to swap boot partitions.

## Development

### Requirements

- PlatformIO Core (`pio`) or VS Code with PlatformIO.
- Python 3.8 or newer.
- USB-C cable.
- SDL2 for simulator builds.

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

### Web Assets

The firmware embeds the HTML and JS from `src/network/html` and `data/js`.

```sh
python3 scripts/build_html.py
```

`pio run` also regenerates these files before compiling.

### Simulator

Inx includes two native simulator targets based on the CrossPoint simulator.

For the full SDL/device UI simulator:

```sh
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator -t run_simulator
```

For dashboard-only testing:

```sh
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator_web -t run_simulator
```

The simulator stores its SD-card data in the folder passed through `CROSSPOINT_SIM_SD`. The firmware web server is exposed at `http://127.0.0.1:8080/` when the simulated device starts a hotspot or local network server.

On macOS, SDL2 is required:

```sh
brew install sdl2
```

For more simulator details, see the [CrossPoint simulator project](https://github.com/crosspoint-reader/crosspoint-simulator).

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
