# resizer2

Rewrite of https://github.com/IsacEkeroth/ahk-resize-windows in C++

## Installation:

1. Download the [latest version of the installer](https://github.com/alvesvaren/resizer2/releases/latest/download/resizer2-setup.exe)
2. Run the installer

> The program is also available as a portable .exe file, which can be ran without installation<br>
> However, autostarting is not automatically set up unless you use the installer

## Uninstalling:

You should be able to uninstall it using the built in uninstallation feature in windows. You will need to close the app before proceeding (in the system tray)

## How to use:

- Win + Left Mouse Button to move windows. If a window is fullscreened or maximized, it will snap between monitors
- Win + Right Mouse Button to resize windows from the closest corner
- Win + Scroll Up/Down to change window opacity
- Win + Middle mouse to minimize a window
- Win + Double click Left Mouse Button to maximize/restore a window

## Extra features:

- Changes the system cursors to reflect what's happening
- Can be closed from the system tray

## Quirks:

- When you normally press Win, the start menu appears, unless you pressed a keyboard shortcut using it.
  So to keep it from appearing, it fakes the combination Win+F13, which usually doesn't do anything.
  However, if you have any hotkeys using F13, you might need to change them or change the key used in this program.
- The program needs to run as administrator in order to support system windows, such as the task manager. If you do not like this, you can disable it in the task scheduler or by running the portable version yourself
- Some fullscreen apps really doesn't like being moved to another monitor, so be careful moving fullscreened windows.
- You can change opacity and move some system bars right now, for example move/resize taskbars on other screens.
  If you accidentally did that, you can restart explorer.exe from task manager and it should fix it.
- Isn't compiled to target 32-bit windows, if you have a 32-bit computer and want to use this, you'll need to compile it yourself

## Autostarting:

When installing using the setup program, you can choose to enable autostarting
