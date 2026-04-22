# Inx 1.0.9 — since 1.0.8

Summary of changes from git tag **1.0.8** to **1.0.9** (current `platformio.ini` version).

## Settings & UI

- **Reader / system settings refresh**: Expanded settings layout, checkbox-style toggles where appropriate, and clearer font controls.
- **Device → “Refresh on load”**: Optional half-refresh after the first paint on Recent, Library, Settings, Sync, and Statistics when enabled.
- **Reader font preview**: Font family shown in the actual typeface; font size shown as a slider-style control with preview text and track (shared between system reader settings and EPUB **Book Settings**).
- **EPUB Book Settings drawer**: Font preview aligned with system settings; binary options use the same checkbox treatment as global settings.

## Images & covers

- More configurable image / bitmap presentation and reader image options.
- Ongoing fixes and tuning for image and cover rendering and cache-related loading.

## Reading & layout

- **Indent**: Per-book and CSS-related indent behavior; drop cap fixes and related layout fixes.
- **CSS**: Parser and style handling improvements.

## Statistics

- **XTC**: Statistics support additions and follow-up fixes.

## KOReader & sync

- KOReader-related fixes bundled with layout/indent work.

## Other

- Bluetooth-related work was explored; Bluetooth manager was **removed for now** (simpler default build).
- General code cleanup and small reverts where needed.

---

*Generated from `git log 1.0.8..HEAD` and recent commit summaries. For the exact file-level diff, run: `git diff 1.0.8..HEAD`.*
