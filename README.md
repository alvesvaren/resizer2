# resizer2

Resize and move windows like in KDE/i3 on Windows with Win+Mouse!

Rewrite of https://github.com/IsacEkeroth/ahk-resize-windows in C++

## Installation:

### Using winget

1. Run `winget install resizer2` in a terminal window

### From github

1. Download the [latest version of the installer](https://github.com/alvesvaren/resizer2/releases/latest/download/resizer2-setup.exe)
2. Run the installer

> The program is also available as a portable .exe file, which can be ran without installation<br><br>
> However, auto starting is not automatically set up unless you use the installer,<br>
> and it doesn't allow you to move system windows unless running as administrator

## Demo

![resizer-demo](https://github.com/user-attachments/assets/b1eb583f-3b3b-413b-b7a4-c431f06baee0)

## How to use:

- Win + Left Mouse Button to move windows. If a window is fullscreened or maximized, it will snap between monitors
- Win + Shift + Left Mouse Button to snap windows. 
  You can snap windows to each quadrant of the screen, or each half of the screen. Move the mouse close to the zone you want to snap to!
- Win + Right Mouse Button to resize windows from the closest corner
- Win + Scroll Up/Down to change window opacity
- Win + Middle mouse to minimize a window
- Win + Double click Left Mouse Button to maximize/restore a window

## Updating

- Download and start the [latest version of the installer](https://github.com/alvesvaren/resizer2/releases/latest/download/resizer2-setup.exe)

## Extra features:

- Changes the system cursors to reflect what's happening
- Can be closed from the system tray

## Quirks:

- To not make the start menu appear, it fakes the combination Win+LShift, which usually doesn't do anything, but might conflict with other hotkeys you have set up.
- It needs to run as administrator to work for system windows (such as the task manager), if you don't want this, use the portable version.
- Some fullscreen apps really doesn't like being moved to another monitor, so depending on the application it may or may not work as expected. Please report any broken apps as an issue, and I'll add it to the blacklist!
- Only precompiled to x64, but should work on arm64 with emulation. 32-bit might work if you compile it yourself!
- It moves the parent window, but some apps are coded incorrectly which makes them behave weird when moved using this.

## Autostarting:

When installing using the setup program, you can choose to enable auto starting (default)

## Uninstalling:

You can uninstall it using the built in uninstallation feature in windows.
