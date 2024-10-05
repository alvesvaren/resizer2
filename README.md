# resizer2

Rewrite of https://github.com/IsacEkeroth/ahk-resize-windows in C++

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
- Some fullscreen apps really doesn't like being moved to another monitor, so be careful moving fullscreened windows.
- You can change opacity and move some system bars right now, for example move/resize taskbars on other screens.
  If you accidentally did that, you can restart explorer.exe from task manager and it should fix it.
- Running multiple instances of the program at the same time is not supported and may break stuff.

## Autostarting

1. Open the "Task Scheduler"
2. Click "Create Basic Task..."
3. Give the task a cool name and press next
4. Set trigger to "When I log on"
5. Keep action at "Start a program"
6. Select the exe file in "Program/script", preferably move the file out from your downloads first
7. Finish creating the task
8. Restart your computer to see if it works
