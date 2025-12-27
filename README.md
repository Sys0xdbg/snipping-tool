# Snipping Tool

A modern, lightweight screenshot tool for Windows 11 with a clean dark UI and blur effects.

![Windows 11](https://img.shields.io/badge/Windows-11-0078D6?logo=windows)
![C++](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus)

## Features

- **Multiple Capture Modes**
  - Rectangle selection
  - Window capture
  - Fullscreen capture
  - Text extraction (OCR)

- **Modern UI**
  - Dark theme with Mica/Acrylic blur effects
  - Smooth animations
  - System tray integration

- **Customizable Hotkeys**
  - Set custom keyboard shortcuts for each capture mode
  - Option to replace Windows built-in Win+Shift+S

- **Auto-save**
  - Configurable save location
  - Optional auto-save without dialog

- **Notifications**
  - Screenshot preview
  - Quick actions: Copy, Open folder, Dismiss

## Installation

### Download
Download the latest release from the [Releases](https://github.com/Sys0xdbg/snipping-tool/releases) page.

### Build from Source

**Requirements:**
- Windows 11 SDK
- Visual Studio 2022 or CMake 3.16+
- DirectX 11

**Build steps:**

```bash
# Clone the repository
git clone https://github.com/Sys0xdbg/snipping-tool.git
cd snipping-tool

# Create build directory
mkdir build
cd build

# Generate and build
cmake ..
cmake --build . --config Release
```

The executable will be in `build/Release/snip.exe`.

## Usage

### Capture Modes

| Mode | Description |
|------|-------------|
| Rectangle | Click and drag to select a region |
| Window | Click on any window to capture it |
| Fullscreen | Capture the entire screen instantly |
| Text (OCR) | Select text area to copy to clipboard |

### Default Hotkeys

| Hotkey | Action |
|--------|--------|
| `Win+Shift+S` | Rectangle capture (when enabled) |
| `Print Screen` | Open mode picker |

### Settings

Access settings by clicking the gear icon in the toolbar:

- **Save Location** - Choose where screenshots are saved
- **Auto-save** - Skip save dialog and save automatically
- **Keyboard Shortcuts** - Configure hotkeys for each mode
- **Replace Windows Snipping Tool** - Intercept Win+Shift+S
- **Start with Windows** - Launch on system startup

### Capture Delay

Click the delay dropdown to set a timer (3s, 5s, or 10s) before capture starts.

## System Tray

The app minimizes to system tray when closed:
- **Left-click** - Show main window
- **Right-click** - Context menu (Show/Exit)

## Requirements

- Windows 11 (for blur effects)
- DirectX 11 compatible GPU

## License

MIT License - See [LICENSE](LICENSE) for details.
