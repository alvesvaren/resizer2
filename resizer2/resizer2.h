#pragma once

#ifndef OEMRESOURCE
#define OEMRESOURCE
#endif

#include <windows.h>
#include <iostream>
#include <array>
#include <chrono>
#include <unordered_set>
#include <vector>
#include <optional>
#include <string>
#include <shellapi.h>
#include <WtsApi32.h>
#include <dwmapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Shcore.lib")

#define TRAY_ICON_UID 1001
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_EXIT 0x5001

// Config
constexpr int DOUBLE_CLICK_THRESHOLD = 300; // ms
constexpr BYTE MIN_OPACITY = 64;
constexpr BYTE MAX_OPACITY = 255;
constexpr BYTE OPACITY_STEP = 26; // Around 10% of 255
constexpr int DUMMY_KEY = 0xE8; // Unassigned virtual key: stops the Start menu opening without taking part in Win+Shift+arrow shortcuts
constexpr double CORNER_RADIUS_FRACTION = 0.33; // 1/3 of monitor dimension

const std::unordered_set<std::wstring> disallowedClasses{
	L"ControlCenterWindow",
	L"TopLevelWindowForOverflowXamlIsland",
	L"XamlExplorerHostIslandWindow",
	L"Windows.UI.Core.CoreWindow",
	L"Shell_InputSwitchDismissOverlay",
	L"Shell_InputSwitchTopLevelWindow",
	L"Shell_SecondaryTrayWnd",
	L"Shell_TrayWnd",
	L"Progman",
	L"WorkerW",
    L"RiotWindowClass"
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
    SIZEWE,
    SIZENS,
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
extern bool g_trayIconAdded;
extern bool g_hooksInstalled;
extern HWND g_hMainWnd;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
DWORD WINAPI WindowOperationThreadProc(LPVOID lpParam);
extern UINT WM_TASKBARCREATED;
void EnsureHooksInstalled();
void ShowTrayMenu(HWND hWnd);

void adjustWindowOpacity(int change);
void minimizeWindow();
template <ContextType type>
void startWindowOperation();
void stopWindowOperation();
HWND getTopLevelParent(HWND hwnd);
void snapToMonitor(HWND window, HMONITOR screen);
std::optional<RECT> getFancyZoneRect(HWND hWnd, HMONITOR hMon, POINT mousePos, bool maximized);
void setWindowSnappedToZone(HWND hWnd, bool snapped);
bool unsnapWindow(HWND hWnd, POINT grab, RECT& rect);
struct ResizeParticipant {
    HWND hwnd;
    RECT start;
    LONG RECT::* xEdge; // edge that follows the mouse horizontally, if any
    LONG RECT::* yEdge; // edge that follows the mouse vertically, if any
};
std::vector<ResizeParticipant> getResizeParticipants(HWND hWnd, LONG RECT::* xEdge, LONG RECT::* yEdge);
template <ResizerCursor cursor>
void SetGlobalCursor();
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
HMONITOR SysGetMonitorContainingPoint(int x, int y);
