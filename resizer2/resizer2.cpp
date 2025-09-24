#define OEMRESOURCE
#include "resizer2.h"
#include "window_ops.h"

static const UINT RETRY_TIMER_ID = 1;
static const UINT RETRY_INTERVAL_MS = 2000; // ms

extern void EnsureHooksInstalled();
static void TryInitialize(HWND hWnd);
extern void ShowTrayMenu(HWND hWnd);
static void EnsureMessageFilter(HWND hWnd);

// Ensure the process is DPI aware so cursor positions and monitor work areas
// use the same coordinate space even when display scaling != 100%.
static void EnableDpiAwareness()
{
    // Prefer Per-Monitor V2 if available (Windows 10+)
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32)
    {
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContext_t)(HANDLE);

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

        auto pSetProcessDpiAwarenessContext = (SetProcessDpiAwarenessContext_t)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext)
        {
            if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            {
                return; // Success
            }
        }
    }

    // Fallback to Per-Monitor (Windows 8.1+)
    HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
    if (hShcore)
    {
        typedef HRESULT (WINAPI *SetProcessDpiAwareness_t)(int);
#ifndef PROCESS_PER_MONITOR_DPI_AWARE
#define PROCESS_PER_MONITOR_DPI_AWARE 2
#endif
        auto pSetProcessDpiAwareness = (SetProcessDpiAwareness_t)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (pSetProcessDpiAwareness)
        {
            if (SUCCEEDED(pSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)))
            {
                return; // Success
            }
        }
    }

    // Last resort: System DPI aware (Vista+)
    HMODULE hUser32b = GetModuleHandleW(L"user32.dll");
    if (!hUser32b) hUser32b = LoadLibraryW(L"user32.dll");
    if (hUser32b)
    {
        typedef BOOL (WINAPI *SetProcessDPIAware_t)();
        auto pSetProcessDPIAware = (SetProcessDPIAware_t)GetProcAddress(hUser32b, "SetProcessDPIAware");
        if (pSetProcessDPIAware)
        {
            pSetProcessDPIAware();
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_TASKBARCREATED) {
		g_trayIconAdded = false; // taskbar recreated; force re-add
		AddTrayIcon(hWnd);
		return 0;
	}

	switch (uMsg) {
	case WM_COMMAND:
		if (LOWORD(wParam) == ID_TRAY_EXIT) {
			PostQuitMessage(0);
			return 0;
		}
		break;
	case WM_WTSSESSION_CHANGE:
		// React to user logon/unlock/console connect by retrying initialization
		switch (wParam) {
		case WTS_SESSION_LOGON:
		case WTS_SESSION_UNLOCK:
		case WTS_CONSOLE_CONNECT:
			TryInitialize(hWnd);
			break;
		default:
			break;
		}
		break;
	case WM_TIMER:
		if (wParam == RETRY_TIMER_ID) {
			TryInitialize(hWnd);
			if (g_hooksInstalled && g_trayIconAdded) {
				KillTimer(hWnd, RETRY_TIMER_ID);
			}
		}
		break;
	case WM_TRAYICON:
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			ShowTrayMenu(hWnd);
		}
		break;

	case WM_DESTROY:
		RemoveTrayIcon();
		WTSUnRegisterSessionNotification(hWnd);
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

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

HMONITOR SysGetMonitorContainingPoint(int x, int y) {
	MonitorSearchData data = { x, y, NULL };

	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&data);

	// If no monitor is found, default to the primary monitor
	if (!data.hMonitor) {
		data.hMonitor = MonitorFromPoint(POINT{ x, y }, MONITOR_DEFAULTTOPRIMARY);
	}

	return data.hMonitor;
}

static void TryInitialize(HWND hWnd) {
	EnsureHooksInstalled();
	AddTrayIcon(hWnd);
}

static void EnsureMessageFilter(HWND hWnd) { }

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    EnableDpiAwareness();

	const wchar_t CLASS_NAME[] = L"TrayIconWindowClass";

	// Both for the uninstaller, and to prevent multiple instances
	HANDLE hMutexHandle = CreateMutex(NULL, FALSE, L"Resizer2Mutex");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		MessageBox(NULL, L"Resizer is already running! Close it before running again.", L"Resizer", MB_ICONINFORMATION | MB_OK);
		return 0;
	}

	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	HWND hWnd = CreateWindowEx(WS_EX_TOOLWINDOW, CLASS_NAME, L"Resizer", WS_POPUP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInstance, NULL);

	if (hWnd == NULL) {
		return 0;
	}
	g_hMainWnd = hWnd;

	WM_TASKBARCREATED = RegisterWindowMessage(TEXT("TaskbarCreated"));
	EnsureMessageFilter(hWnd);

	// Register for session change notifications (this session)
	WTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);

	// Attempt initialization immediately, then keep retrying until success
	TryInitialize(hWnd);
	if (!g_hooksInstalled || !g_trayIconAdded) {
		SetTimer(hWnd, RETRY_TIMER_ID, RETRY_INTERVAL_MS, NULL);
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (hKeyboardHook) UnhookWindowsHookEx(hKeyboardHook);
	if (hMouseHook) UnhookWindowsHookEx(hMouseHook);

	return 0;
}
