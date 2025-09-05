#define OEMRESOURCE
#include "resizer2.h"
#include "window_ops.h"

static const UINT RETRY_TIMER_ID = 1;
static const UINT RETRY_INTERVAL_MS = 2000; // ms

extern void EnsureHooksInstalled();
static void TryInitialize(HWND hWnd);
extern void ShowTrayMenu(HWND hWnd);
static void EnsureMessageFilter(HWND hWnd);

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
	const wchar_t CLASS_NAME[] = L"TrayIconWindowClass";

	// Both for the uninstaller, and to prevent multiple instances
	HANDLE hMutexHandle = CreateMutex(NULL, FALSE, L"Resizer2Mutex");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		MessageBox(NULL, L"Resizer is already running!", L"Resizer", MB_ICONINFORMATION | MB_OK);
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
