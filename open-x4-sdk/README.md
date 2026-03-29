# OpenX4 E-Paper Community SDK

A **community-maintained SDK** for building firmware and tools for the **Xteink X4** device. 
This repository is designed to be included as a **Git submodule** inside PlatformIO projects, providing a shared set of 
libraries, utilities, and development workflows that make working with the X4 simple and consistent.

## ✨ **What is this?**

The **OpenX4 E-Paper Community SDK** provides:

* **Common PlatformIO-friendly components** for the Xteink X4
* **Reusable libraries** for display control, graphics, hardware helpers, utilities, etc.
* **Tools** to support flashing, packaging, testing, and device workflows
* A central place for **community contributions**, improvements, and shared knowledge

The SDK is intentionally modular - bring it into your project and use only what you need.

## 📁 Repository Structure

```
community-sdk/
├── libs/           # Reusable components for X4 firmware
│   ├── display/       # E-paper helpers & drivers
│   ├── graphics/      # Drawing, fonts, UI utilities
│   ├── hardware/      # GPIO, power, sensors, timings, etc.
│   └── ...            # Add new modules here!
│
└── tools/          # Dev tools for X4
    ├── flash/         # Flash helpers, scripts, workflows
    ├── assets/        # Conversion tools for images/fonts
    └── ...            # Community-contributed utilities
```

Each lib aims to be **self-contained**, **documented**, and **PlatformIO-friendly**.
Libs should be categorized under `libs/` based on functionality, and then contained within a directory under that root.

## 📦 Adding to Your PlatformIO Project

Add this repository as a submodule:

```bash
git submodule add https://github.com/open-x4-epaper/community-sdk.git open-x4-sdk
```

Then add each lib you need into your `platformio.ini` file as `lib_deps`:

```ini
lib_deps =
  BatteryMonitor=symlink://open-x4-sdk/libs/hardware/BatteryMonitor
  EpdScreenController=symlink://open-x4-sdk/libs/display/EpdScreenController
```

Then you can include the libraries in your project as usual:

```cpp
#include <BatteryMonitor.h>
#include <EpdScreenController.h>
`````

Or load tools from the `tools/` directory as needed.

## 🤝 Contributing

This is a **community-driven project** - contributions are not only welcome but encouraged!

Ways you can help:

* Improve or extend existing libraries
* Add new modules to `libs/`
* Build utilities for the `tools/` directory
* Report issues, propose features, or help refine the API
* Improve documentation

### Contribution guidelines (short version)

1. Keep modules self-contained
2. Prefer zero-dependency solutions where practical
3. Document your additions
4. Use clear naming and consistent structure
5. Be friendly and constructive in PR conversations

A full contributing guide will be added as the project grows.

## 📝 License

This SDK is released under an open-source MIT license. To keep things simple, all contributions and code must also 
fall under this license.

## 💬 Community

Feel free to open GitHub issues for support, improvements, or discussion around the Xteink X4 ecosystem.
Join the discord here: https://discord.gg/2cdKUbWRE8
