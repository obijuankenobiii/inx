# Inx

Reimagined. Improved. Simplified.

*This project is a fork of CrossPoint and is not affiliated with Xteink; it's built as a community project.*

---

![](./docs/images/cover.jpg)

## Features & functions

This section lists what the firmware actually implements (navigation, screens, settings, readers, and utilities).

### Navigation

- **Tab bar** (left/right on supported layouts): **Recent** → **Library** → **Settings** → **Sync** → **Statistics**, then wraps.
- **Settings** opens directly into grouped **System** or **Reader** lists (expand/collapse sections with **Confirm** on a section header). **Back** swaps between **System** and **Reader** panels (hint shows the other panel name).
- **Power**: configurable short press (sleep / page refresh / ignore) and **long hold** to sleep; idle **time to sleep** returns to the sleep screen.

### Recent (home)

- **Recent Library Mode** (system setting): **Grid**, **List Stats**, or **Flow**.
- **Tabs from Recent**: **Library** (opens library at `/`), **Statistics**, **Settings**; select a book to open the **Reader** for that file.

### Library

- **File explorer** with path navigation, **nested folders**, **A–Z sort**.
- **Favorites** and book selection to open the reader.
- Optional **library index** (see Settings → **Index your library** and **Use Index for Library**).

### Statistics

- Reading / book statistics view (from the Statistics tab).

### Sync (network hub)

From the **Sync** tab:

- **Join a Network** – Wi‑Fi station setup (`LocalNetworkActivity`); after connect, a small **`LocalServer`** HTTP/Web UI can run for file upload from the browser (see that activity’s flow).
- **Connect to Calibre** – wireless Calibre device flow (`CalibreConnectActivity`).
- **Create Hotspot** – device as AP (`HotspotActivity`).
- **Bluetooth** – Bluetooth setup (`BluetoothActivity`).

File transfer / device workflows return to Sync or related callbacks as implemented.

### Settings

Settings are two panels (**System** and **Reader**), each a grouped list. Sections start **collapsed**; open a section to change items.

#### System panel

| Group | Items |
|--------|--------|
| **Display & sleep** | Sleep Screen (Dark, Light, Custom, Recent Book, Transparent Cover, None); Sleep Screen Cover Mode (Fit, Crop); Sleep Screen Cover Filter (None, Contrast, Inverted); Hide Battery % (Never, In Reader, Always); Recent Library Mode (Grid, List Stats, Flow) |
| **Buttons** | Front Button Layout (four layouts mapping Back / Confirm / Left / Right); Short Power Button Click (Ignore, Sleep, Page Refresh) |
| **Device & library** | Time to Sleep (1–30 min); **Use Index for Library** (toggle); Boot Mode (Recent Books, Home Page) |
| **Actions** | **Index your library**; **KOReader Sync**; **OPDS Browser**; **Clear Cache**; **Check for updates** (opens OTA flow with Wi‑Fi selection) |
| **About** | **About** is a **standalone row** at the bottom of System settings (not inside a collapsible group). Opens the modal with **Current version** and an **Update** button; **Update** (Confirm) starts the same OTA path as **Check for updates** (Wi‑Fi, then GitHub / HTTPS OTA). |

#### Reader panel (global reading defaults)

| Group | Items |
|--------|--------|
| **Font** | Font Family (Bookerly, Atkinson Hyperlegible, Literata); Font Size (Extra Small → X Large) |
| **Layout** | Line Spacing (Tight, Normal, Wide); Screen Margin (5–80); Paragraph Alignment (Justify, Left, Center, Right); Extra Paragraph Spacing; Reading Orientation (Portrait, Landscape CW, Inverted, Landscape CCW); Hyphenation |
| **Navigation** | Next & Previous Mapping (Left/Right, Right/Left, Up/Down, Down/Up, None); Book Settings Toggle (which physical button opens the in-book menu); Long-press Chapter Skip; Short Power Button in reader (Page Turn, Page Refresh); Page Auto Turn (0–180 s, step 10) |
| **System** (reader rendering) | Text Anti-Aliasing; Refresh Frequency (1 / 5 / 10 / 15 / 30 pages) |
| **Status bar** | Status Bar Mode (None, No Progress, Full w/ Percentage, Full w/ Progress Bar, Progress Bar, Battery %, Percentage, Page Bars); **Left / Middle / Right** sections each: None, Page Numbers, Percentage, Chapter Title, Battery Icon, Battery %, Battery Icon+%, Progress Bar, Progress Bar+%, Page Bars, Book Title, Author Name |

#### Related settings flows (activities)

- **KOReader Sync** – settings and credential entry (`KOReaderSettingsActivity`); **Wi‑Fi** and **KOReaderAuthActivity** when needed; uses **`KeyboardEntryActivity`** for text fields.
- **OPDS Browser** – browse and download from OPDS catalogs (`OpdsBookBrowserActivity`); may use **Wi‑Fi** (`WifiSelectionActivity`) and keyboard entry for URLs/credentials.
- **Clear Cache** – clear cached data (`ClearCacheActivity`).
- **Check for updates** / **About → Update** – **`OtaUpdateActivity`**: turns on Wi‑Fi, runs **`WifiSelectionActivity`**, then **`OtaUpdater`** (GitHub releases API, optional HTTPS install). Same OTA stack as the **Check for updates** row under Actions.

### Reading

- **Formats**: **EPUB** (2/3), **XTC** / **XTCH**, **TXT** / **MD** – routed by extension in **`ReaderActivity`** into **`EpubActivity`**, **`XtcReaderActivity`**, or **`TxtReaderActivity`**.
- **EPUB**
  - **Paging**, **saved position**, **status bar**, **chapter** navigation, configurable **page auto-turn**.
  - **Bookmarks**: long-press **Confirm** to add; **`BookmarkActivity`** to list, open, or remove entries.
  - **Menu drawer** (short **Confirm**): book title, **TOC / chapter list** (drawer UI), **bookmarks**, **go home** (close book), **delete cache** / **delete progress** / **delete book**, **regenerate full book layout** (rebuild cached layout data).
  - **Settings drawer** (mapped **Book Settings Toggle** button): per-book overrides aligned with global reader settings (font, layout, controls, status bar).
  - **KOReader sync** (when launched from the EPUB flow): **`KOReaderSyncActivity`** (Wi‑Fi via **`WifiSelectionActivity`** as needed).
  - **Short Power** in reader: page turn or screen refresh (per reader settings).
  - **Back**: long hold exits the book (short hold behavior depends on context / drawers).
- **XTC**: **`XtcReaderActivity`**; chapter flow may open **`XtcReaderChapterSelectionActivity`**.
- **TXT**: **`TxtReaderActivity`** with scrolling / paging behavior as implemented there.

### System & lifecycle

- **Boot** – startup / splash path (`BootActivity`).
- **Sleep** – **`SleepActivity`** and **`HalDisplay` deep sleep**; sleep **image** driven by **Sleep Screen** (dark, light, custom, recent book, transparent cover, none); cover **fit/crop** and **filter**; **auto sleep** after **Time to Sleep**; **USB-only wake** can return to deep sleep from `setup()`.
- **SD card missing / error** – **`FullScreenMessageActivity`** with a fixed message until SD is fixed.
- **Keyboard entry** – **`KeyboardEntryActivity`** for Wi‑Fi passwords, OPDS/Calibre/KOReader fields, and similar prompts.
- **Activity switch logging** – `switchTo` in `main.cpp` prints **heap / fragmentation** over serial for debugging.

### Complete screen & activity reference

Rough inventory of major UI entry points (see `src/activity/` and `src/main.cpp` for wiring):

| Area | Components |
|------|------------|
| **Shell** | `BootActivity`, `SleepActivity`, `FullScreenMessageActivity` |
| **Tabs** | `RecentActivity`, `LibraryActivity`, `SettingsActivity`, `SyncActivity`, `StatisticActivity` |
| **Reader** | `ReaderActivity` → `EpubActivity` \| `XtcReaderActivity` \| `TxtReaderActivity` |
| **EPUB UI** | `MenuDrawer` (TOC + actions), `SettingsDrawer`, `BookmarkActivity` |
| **XTC** | `XtcReaderChapterSelectionActivity` |
| **Settings detail** | `CategorySettingsActivity`; `AboutPage` (modal); `KOReaderSettingsActivity`, `KOReaderAuthActivity`, `CalibreSettingsActivity`, `ClearCacheActivity`, `OtaUpdateActivity` |
| **Network** | `LocalNetworkActivity`, `CalibreConnectActivity`, `HotspotActivity`, `BluetoothActivity`, `WifiSelectionActivity` |
| **Browser** | `OpdsBookBrowserActivity` |
| **KOReader (book)** | `KOReaderSyncActivity` |
| **Utilities** | `KeyboardEntryActivity` |

---

## Installing

### Web (specific firmware version)

1. Connect your Xteink X4 to your computer via USB-C
2. Download the `firmware.bin` file from the release of your choice via the [releases page](https://github.com/obijuankenobiii/inx/releases)
3. Go to https://xteink.dve.al/ and flash the firmware file using the "OTA fast flash controls" section

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Manual

See [Development](#development) below.

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Checking out the code

Inx uses PlatformIO for building and flashing the firmware. To get started, clone the repository:

```
git clone --recursive https://github.com/obijuankenobiii/inx

# Or, if you've already cloned without --recursive:
git submodule update --init --recursive
```

### Flashing your device

Connect your Xteink X4 to your computer via USB-C and run the following command.

```sh
pio run --target upload
```
### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```
after that run the script:
```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```
Minor adjustments may be required for Windows.

## Internals

INX is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only
has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based
on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the 
cache. This cache directory exists at `.metadata` on the SD card. The structure is as follows:


```
.metadata/
├── 12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── setting.bin     # Stores reading progress (chapter, page, etc.)
│   ├── statistics.bin     # Stores reading statistics (chapter count, page count, session count, time read)
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/        # All chapter data is stored in the sections subdirectory
│       ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin        #     files are named by their index in the spine
│       └── ...
│
└── 189013891/
```

Deleting the `.metadata` directory will clear the entire cache. 


## Contributing

Contributions are very welcome!


### To submit a contribution:

1. Fork the repo
2. Create a branch (`feature/dithering-improvement`)
3. Make changes
4. Submit a PR

