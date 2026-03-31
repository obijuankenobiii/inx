# Inx

Reimagined. Improved. Simplified.

*This project is a fork of CrossPoint and is not affiliated with Xteink; it's built as a community project.*

---

![](./docs/images/cover.jpg)

## Pages

- **Home** – Grid and statistics view modes for quick access
- **Library** – All your books, organized with file explorer support
- **Settings** – Individual and book-specific configuration options
- **Sync** – WiFi book upload and OTA updates
- **Statistics** – Reading insights and system usage data

---

### eBook Support

- [x] EPUB parsing and rendering (EPUB 2 and EPUB 3)
  - [x] Image support within EPUB (JPG, PNG)
  - [x] Saved reading position
  - [x] Bookmarks
- [x] XTC format support
- [x] TXT format support

---

### File Management

- [x] File explorer with file picker
- [x] Sorting [A-Z]
- [x] Support nested folders
- [x] Folder and list mode
- [x] Favorites

---

### Display

- [x] Screen rotation
- [x] Custom sleep screen
  - [x] Cover sleep screen
  - [x] Recent book sleep screen
  - [x] Transparent sleep screen

---

### Wireless

- [x] WiFi book upload
- [x] WiFi OTA updates

---

### Menu Navigation

Simplified menu navigation with left and right swipes to move between pages. Clean, intuitive, and designed for one-handed use.

---

### Settings

#### Global Settings (System)

**Display**
- Sleep Screen (Dark, Light, Custom, Recent Book, Transparent Cover, None)
- Sleep Screen Cover Mode (Fit, Crop)
- Sleep Screen Cover Filter (None, Contrast, Inverted)

**Battery**
- Hide Battery % (Never, In Reader, Always)

**Library**
- Recent Library Mode (Grid, Default)
- Use Index for Library (On/Off)

**Buttons**
- Front Button Layout (Bck Cnfrm Lft Rght / Lft Rght Bck Cnfrm / Lft Bck Cnfrm Rght / Bck Cnfrm Rght Lft)
- Short Power Button Click (Ignore, Sleep, Page Refresh)

**System**
- Time to Sleep (1 min, 5 min, 10 min, 15 min, 30 min)
- Boot Mode (Recent Books, Home Page)

**Actions**
- KOReader Sync
- OPDS Browser
- Clear Cache
- Check for updates

---

#### Global Settings (Book)

**Font**
- Font Family (Bookerly, Atkinson Hyperlegible, Literata)
- Font Size (Extra Small, Small, Medium, Large, X Large)

**Layout**
- Line Spacing (Tight, Normal, Wide)
- Screen Margin (5-80)
- Paragraph Alignment (Justify, Left, Center, Right)
- Extra Paragraph Spacing (On/Off)
- Reading Orientation (Portrait, Landscape CW, Inverted, Landscape CCW)
- Hyphenation (On/Off)

**Navigation**
- Next & Previous Mapping (Left/Right, Right/Left, Up/Down, Down/Up, None)
- Book Settings Toggle (Up, Down, Left, Right, Confirm)
- Long-press Chapter Skip (On/Off)
- Short Power Button (Page Turn, Page Refresh)

**Display**
- Text Anti-Aliasing (On/Off)
- Refresh Frequency (1 page, 5 pages, 10 pages, 15 pages, 30 pages)

**Status Bar**

**Status Bar Mode**
- None
- No Progress
- Full w/ Percentage
- Full w/ Progress Bar
- Progress Bar
- Battery %
- Percentage
- Page Bars

**Left / Middle / Right Section**
- None
- Page Numbers
- Percentage
- Chapter Title
- Battery Icon
- Battery %
- Battery Icon+%
- Progress Bar
- Progress Bar+%
- Page Bars
- Book Title
- Author Name
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

CrossPoint uses PlatformIO for building and flashing the firmware. To get started, clone the repository:

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

