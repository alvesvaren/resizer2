# resizer2

Rewrite of https://github.com/IsacEkeroth/ahk-resize-windows in C++

## Installation:

1. Download the [latest version of the installer](https://github.com/alvesvaren/resizer2/releases/latest/download/resizer2-setup.exe)
2. Run the installer

> The program is also available as a portable .exe file, which can be ran without installation<br><br>
> However, autostarting is not automatically set up unless you use the installer,<br>
> and it doesn't allow you to move system windows unless started as administrator

## Demo

![resizer-demo](https://github.com/user-attachments/assets/b1eb583f-3b3b-413b-b7a4-c431f06baee0)

## How to use:

- Win + Left Mouse Button to move windows. If a window is fullscreened or maximized, it will snap between monitors
- Win + Right Mouse Button to resize windows from the closest corner
- Win + Scroll Up/Down to change window opacity
- Win + Middle mouse to minimize a window
- Win + Double click Left Mouse Button to maximize/restore a window

## Updating

- Stop the resizer2.exe file either from task manager or the system tray
- Download and start the updated installer from github

## Extra features:

- Changes the system cursors to reflect what's happening
- Can be closed from the system tray

## Quirks:

- When you normally press Win, the start menu appears, unless you pressed a keyboard shortcut using it.
  So to keep it from appearing, it fakes the combination Win+F13, which usually doesn't do anything.
  However, if you have any hotkeys using F13, you might need to change them or change the key used in this program (by compiling it yourself)
- The program needs to run as administrator in order to support system windows, such as the task manager. If you do not like this, you can disable it in the task scheduler or by running the portable version yourself
- Some fullscreen apps really doesn't like being moved to another monitor, so be careful moving fullscreened windows.
- Isn't compiled to target 32-bit windows, if you have a 32-bit computer and want to use this, you'll need to compile it yourself
- I use windows 11, I've tested it on 10 but there are some bugs, like being able to move the desktop icons that isn't intended. As windows 10 is going EOL next year I'll not spend time fixing it but feel free to submit a PR if it bothers you :P
- It moves the parent window always, but some apps uses child windows incorrectly which makes it impossible to move them using this, I'm not sure if there's a good way to fix that without breaking other programs that use nested windows inside to allow for resizing parts etc

## Autostarting:

When installing using the setup program, you can choose to enable autostarting

## Uninstalling:

You should be able to uninstall it using the built in uninstallation feature in windows. You will need to close the app before proceeding (in the system tray, or in task manager if it doesn't show up there)
