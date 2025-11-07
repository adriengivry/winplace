# winplace

A lightweight X11 utility to move and resize windows with automatic frame compensation.

## Features

- Move and resize windows by visible area (excluding shadows/borders)
- Automatic compensation for window decorations (`_GTK_FRAME_EXTENTS`, `_NET_FRAME_EXTENTS`)
- Target windows by name or use the currently active window
- Unmaximizes windows automatically before repositioning

## Installation

```bash
make
sudo make install
```

## Usage

```bash
winplace <x> <y> <width> <height> [window_name]
```

### Examples

Position the active window:
```bash
winplace 0 0 960 1080
```

Position a specific window by name:
```bash
winplace 0 0 960 1080 "Firefox"
winplace 960 0 960 1080 "Terminal"
```

## Building

Requires X11 development libraries:
```bash
# Debian/Ubuntu
sudo apt-get install libx11-dev

# Fedora/RHEL
sudo dnf install libX11-devel

# Arch
sudo pacman -S libx11
```

Then build:
```bash
make
```

## License

This project is provided as-is for public use.
