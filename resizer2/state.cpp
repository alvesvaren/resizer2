#include "resizer2.h"

Context ctx;
HHOOK hKeyboardHook = NULL;
HHOOK hMouseHook = NULL;
bool modKeyPressed = false;
bool didUseWindowsKey = false;
bool shouldMinimize = false;
std::chrono::steady_clock::time_point lastClickTime;
NOTIFYICONDATA nid{};
bool g_trayIconAdded = false;
bool g_hooksInstalled = false;
HWND g_hMainWnd = NULL;
UINT WM_TASKBARCREATED = 0;


