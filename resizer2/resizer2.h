#pragma once

#include <windows.h>
#include <iostream>
#include <array>
#include <chrono>
#include <unordered_set>
#include <atlbase.h>
#include <string>

#pragma comment(lib, "user32.lib")

#define TRAY_ICON_UID 1001
#define WM_TRAYICON (WM_USER + 1)

// Config
const int DOUBLE_CLICK_THRESHOLD = 300; // ms
const BYTE MIN_OPACITY = 64;
const BYTE MAX_OPACITY = 255;
const BYTE OPACITY_STEP = 26; // Around 10% of 255
const int DUMMY_KEY = VK_F13; // Any key that doesn't do anything when pressed together with the Windows key

const std::unordered_set<std::string> disallowedClasses{
    "ControlCenterWindow",
    "TopLevelWindowForOverflowXamlIsland",
    "XamlExplorerHostIslandWindow",
    "Windows.UI.Core.CoreWindow",
    "Shell_InputSwitchDismissOverlay",
    "Shell_InputSwitchTopLevelWindow",
    "Shell_SecondaryTrayWnd",
    "Shell_TrayWnd",
    "Progman",
	"WorkerW",
};

enum ContextType {
    MOVE,
    RESIZE
};

struct Context {
    bool inProgress = false;
    HANDLE hEvent = NULL;
    POINT startMousePos{};
    RECT startWindowRect{};
    HWND targetWindow = NULL;
    ContextType operationType;
};

struct MonitorSearchData {
    int x, y;
    HMONITOR hMonitor;
};

enum ResizerCursor {
    SIZEALL,
    SIZENWSE,
    SIZENESW,
    UNSET,
};

// List of all system cursors that can be set
extern const std::array<int, 13> systemCursors;

extern Context ctx;
extern HHOOK hKeyboardHook;
extern HHOOK hMouseHook;
extern bool modKeyPressed;
extern bool didUseWindowsKey;
extern bool shouldMinimize;
extern std::chrono::steady_clock::time_point lastClickTime;
extern NOTIFYICONDATA nid;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
DWORD WINAPI WindowOperationThreadProc(LPVOID lpParam);
UINT WM_TASKBARCREATED;

void adjustWindowOpacity(int change);
void minimizeWindow();
template <ContextType type>
void startWindowOperation();
void stopWindowOperation();
HWND getTopLevelParent(HWND hwnd);
void snapToMonitor(HWND window, HMONITOR screen);
template <ResizerCursor cursor>
void SetGlobalCursor();
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
HMONITOR SysGetMonitorContainingPoint(int x, int y);
