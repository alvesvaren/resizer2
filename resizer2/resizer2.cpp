// main.cpp
#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <chrono>
#include <Psapi.h>
#include <tchar.h>

#pragma comment(lib, "user32.lib")

// Constants
const int DOUBLE_CLICK_THRESHOLD = 300; // in milliseconds
const BYTE MIN_OPACITY = 64;
const BYTE MAX_OPACITY = 255;
const BYTE OPACITY_STEP = 26; // Approximately 10% of 255

// Global Variables
HHOOK hKeyboardHook = NULL;
HHOOK hMouseHook = NULL;
bool modKeyPressed = false; // Left Windows key
bool didUseWindowsKey = false;
std::chrono::steady_clock::time_point lastClickTime;

// Enum for operation types
enum ContextType {
    MOVE,
    RESIZE
};

// Forward Declarations
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
DWORD WINAPI WindowOperationThreadProc(LPVOID lpParam);
void adjustWindowOpacity(int change);
void increaseOpacity();
void decreaseOpacity();
void minimizeWindow();

template <ContextType type>
void startWindowOperation();

void stopWindowOperation();
void SetWindowMaximized(HWND window, bool maximized);
void SnapToMonitor(HWND window, HMONITOR screen);

static HWND GetTopLevelParent(HWND hwnd) {
    HWND parent = hwnd;
    HWND tmp;
    while ((tmp = GetParent(parent)) != NULL) {
        parent = tmp;
    }
    return parent;
}

struct MonitorSearchData {
    int x, y;
    HMONITOR hMonitor;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MonitorSearchData* data = (MonitorSearchData*)dwData;

    // Check if the point is within the current monitor's bounds
    if (data->x >= lprcMonitor->left && data->x <= lprcMonitor->right &&
        data->y >= lprcMonitor->top && data->y <= lprcMonitor->bottom) {
        // Update the monitor handle in the search data
        data->hMonitor = hMonitor;
        return FALSE; // Stop enumerating after finding the monitor
    }

    return TRUE; // Continue enumerating if not found
}

// Function to find the monitor containing the point
static HMONITOR SysGetMonitorContainingPoint(int x, int y) {
    MonitorSearchData data = { x, y, NULL };

    // Enumerate monitors and find which one contains the point
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&data);

    // If no monitor is found, default to the primary monitor
    if (!data.hMonitor) {
        data.hMonitor = MonitorFromPoint(POINT{ x, y }, MONITOR_DEFAULTTOPRIMARY);
    }

    return data.hMonitor;
}

// Struct to hold operation context
struct Context {
    bool inProgress = false;
    HANDLE hEvent = NULL;
    POINT startMousePos;
    RECT startWindowRect;
    HWND targetWindow = NULL;
    ContextType operationType;
};

// Global operation context
Context ctx;

int main() {
    // Set up keyboard and mouse hooks
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);

    if (!hKeyboardHook || !hMouseHook) {
        std::cerr << "Failed to install hooks!" << std::endl;
        return 1;
    }

    std::cout << "Initialized" << std::endl;

    // Message loop to keep the program running
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Process messages if needed
    }

    // Unhook before exiting
    UnhookWindowsHookEx(hKeyboardHook);
    UnhookWindowsHookEx(hMouseHook);

    return 0;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* pKbDllHookStruct = (KBDLLHOOKSTRUCT*)lParam;
    if (nCode == HC_ACTION) {
        if (pKbDllHookStruct->vkCode == VK_LWIN && pKbDllHookStruct->flags) {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (!modKeyPressed) {
                    didUseWindowsKey = false;  // Reset usage flag on key down
                }
                modKeyPressed = true;

                //return 1;  // Suppress the Windows key until we know if it's used
            }
            else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                modKeyPressed = false;
                if (didUseWindowsKey) {
                    // Win + F12 does nothing, ugly workaround which stops the windows menu from appearing

                    INPUT inputs[2] = {};
                    ZeroMemory(inputs, sizeof(inputs));

                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_F12;

                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = VK_F12;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

                    UINT uSent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
                //return 1;  // Suppress the key event in either case
            }
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

void SetWindowMaximized(HWND window, bool maximized) {
    ShowWindow(window, maximized ? SW_RESTORE : SW_MAXIMIZE);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    MSLLHOOKSTRUCT* pMsLlHookStruct = (MSLLHOOKSTRUCT*)lParam;
    if (nCode == HC_ACTION) {

        switch (wParam) {
        case WM_MOUSEWHEEL: {
            if (modKeyPressed) {
                didUseWindowsKey = true;
                short delta = GET_WHEEL_DELTA_WPARAM(pMsLlHookStruct->mouseData);
                if (delta > 0) {
                    increaseOpacity();
                }
                else if (delta < 0) {
                    decreaseOpacity();
                }
                return 1; // Suppress further processing
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (modKeyPressed && !ctx.inProgress) {
                didUseWindowsKey = true;
                startWindowOperation<MOVE>();
                return 1;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (ctx.inProgress && ctx.operationType == MOVE) {
                didUseWindowsKey = true;
                stopWindowOperation();
                return 1;
            }
            break;
        }
        case WM_RBUTTONDOWN: {
            if (modKeyPressed && !ctx.inProgress) {
                didUseWindowsKey = true;
                startWindowOperation<RESIZE>();
                return 1;
            }
            break;
        }
        case WM_RBUTTONUP: {
            if (ctx.inProgress && ctx.operationType == RESIZE) {
                didUseWindowsKey = true;
                stopWindowOperation();
                return 1;
            }
            break;
        }
        case WM_MBUTTONUP: {
            if (modKeyPressed) {
                didUseWindowsKey = true;
                minimizeWindow();
                return 1;
            }
            break;
        }
        default:
            break;
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

// Adjust window opacity
void adjustWindowOpacity(int change) {
    POINT pt;
    GetCursorPos(&pt);
    HWND hwnd = GetTopLevelParent(WindowFromPoint(pt));
    if (hwnd != NULL) {
        BYTE currentOpacity = MAX_OPACITY;
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_LAYERED) {
            BYTE alpha;
            DWORD flags;
            GetLayeredWindowAttributes(hwnd, NULL, &alpha, &flags);
            currentOpacity = alpha;
        }
        int newOpacity = max(MIN_OPACITY, min(MAX_OPACITY, currentOpacity + change));
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)newOpacity, LWA_ALPHA);
    }
}

void increaseOpacity() {
    adjustWindowOpacity(OPACITY_STEP);
}

void decreaseOpacity() {
    adjustWindowOpacity(-OPACITY_STEP);
}

// Minimize window under cursor
void minimizeWindow() {
    POINT pt;
    GetCursorPos(&pt);
    HWND hwnd = WindowFromPoint(pt);
    hwnd = GetTopLevelParent(hwnd);
    if (hwnd != NULL) {
        ShowWindow(hwnd, SW_MINIMIZE);
    }
}

template<ContextType type>
void startWindowOperation() {
    ctx.inProgress = true;
    ctx.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ctx.operationType = type;

    if (!ctx.hEvent) {
        ctx.inProgress = false;
        return;
    }

    // Get the window under the cursor
    GetCursorPos(&ctx.startMousePos);
    ctx.targetWindow = WindowFromPoint(ctx.startMousePos);

    if (ctx.targetWindow == NULL) {
        ctx.inProgress = false;
        CloseHandle(ctx.hEvent);
        ctx.hEvent = NULL;
        return;
    }

    // Get the top-level parent window
    ctx.targetWindow = GetTopLevelParent(ctx.targetWindow);

    // Double-click detection for move operation
    if (ctx.operationType == MOVE) {
        auto currentTime = std::chrono::steady_clock::now();
        auto timeSinceLastClick = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastClickTime).count();

        if (timeSinceLastClick <= DOUBLE_CLICK_THRESHOLD) {
            // Double-click detected, toggle maximize/restore
            WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
            GetWindowPlacement(ctx.targetWindow, &wp);
            SetWindowMaximized(ctx.targetWindow, wp.showCmd == SW_MAXIMIZE);

            ctx.inProgress = false;
            CloseHandle(ctx.hEvent);
            ctx.hEvent = NULL;
            return;
        }
        lastClickTime = currentTime;
    }

    GetWindowRect(ctx.targetWindow, &ctx.startWindowRect);
    SetForegroundWindow(ctx.targetWindow);

    // Create thread to handle operation
    HANDLE hThread = CreateThread(NULL, 0, WindowOperationThreadProc, NULL, 0, NULL);
    if (hThread != NULL) {
        CloseHandle(hThread);
    }
}

void stopWindowOperation() {
    ctx.inProgress = false;
    if (ctx.hEvent != NULL) {
        SetEvent(ctx.hEvent);
        CloseHandle(ctx.hEvent);
        ctx.hEvent = NULL;
    }
}

void SnapToMonitor(HWND window, HMONITOR screen) {
    MONITORINFO info{};
    info.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(screen, &info);

    RECT rect = info.rcWork; // Work area, excluding taskbars

    HMONITOR currentScreen = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (currentScreen == screen) {
        SendMessage(window, WM_SETREDRAW, TRUE, 0); // Re-enable redraw
        RedrawWindow(window, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE); // Force redraw
        return;
    }

    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    GetWindowPlacement(window, &wp);

    // Move the maximized window to the new monitor
    wp.showCmd = SW_MAXIMIZE;  // Keep it maximized
    wp.rcNormalPosition = info.rcWork;
    SetWindowPlacement(window, &wp);

    // Move the window to the new monitor without restoring it
    SetWindowPos(window, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOZORDER);
}

DWORD WINAPI WindowOperationThreadProc(LPVOID lpParam) {
    RECT currentWindowRect = ctx.startWindowRect;

    if (ctx.operationType == MOVE) {
        // Handle moving
        WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
        GetWindowPlacement(ctx.targetWindow, &wp);
        bool maximized = wp.showCmd == SW_MAXIMIZE;

        while (ctx.inProgress) {
            if (WaitForSingleObject(ctx.hEvent, 0) == WAIT_OBJECT_0) {
                break;
            }

            POINT pt;
            GetCursorPos(&pt);
            int dx = pt.x - ctx.startMousePos.x;
            int dy = pt.y - ctx.startMousePos.y;

            if (maximized) {
                HMONITOR screen = SysGetMonitorContainingPoint(pt.x, pt.y);
                SnapToMonitor(ctx.targetWindow, screen);
            }
            else {
                MoveWindow(ctx.targetWindow,
                    currentWindowRect.left + dx, currentWindowRect.top + dy,
                    currentWindowRect.right - currentWindowRect.left,
                    currentWindowRect.bottom - currentWindowRect.top, TRUE);
            }
        }
    }
    else if (ctx.operationType == RESIZE) {
        // Handle resizing
        bool isLeft = false, isTop = false;
        int windowWidth = currentWindowRect.right - currentWindowRect.left;
        int windowHeight = currentWindowRect.bottom - currentWindowRect.top;

        // Determine which corner is nearest
        if (ctx.startMousePos.x - currentWindowRect.left < windowWidth / 2)
            isLeft = true;
        if (ctx.startMousePos.y - currentWindowRect.top < windowHeight / 2)
            isTop = true;

        while (ctx.inProgress) {
            if (WaitForSingleObject(ctx.hEvent, 0) == WAIT_OBJECT_0) {
                break;
            }
            POINT pt;
            GetCursorPos(&pt);
            int dx = pt.x - ctx.startMousePos.x;
            int dy = pt.y - ctx.startMousePos.y;

            RECT newRect = currentWindowRect;
            if (isLeft) {
                newRect.left += dx;
            }
            else {
                newRect.right += dx;
            }
            if (isTop) {
                newRect.top += dy;
            }
            else {
                newRect.bottom += dy;
            }

            MoveWindow(ctx.targetWindow, newRect.left, newRect.top,
                newRect.right - newRect.left, newRect.bottom - newRect.top, TRUE);

            Sleep(1); // Small delay to prevent high CPU usage
        }
    }

    ctx.inProgress = false;
    return 0;
}
