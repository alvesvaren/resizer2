#define OEMRESOURCE
#include "resizer2.h"

const std::array<int, 13> systemCursors{{
    OCR_NORMAL,
    OCR_IBEAM,
    OCR_WAIT,
    OCR_CROSS,
    OCR_UP,
    OCR_SIZENWSE,
    OCR_SIZENESW,
    OCR_SIZEWE,
    OCR_SIZENS,
    OCR_SIZEALL,
    OCR_NO,
    OCR_HAND,
    OCR_APPSTARTING,
}};

Context ctx;
HHOOK hKeyboardHook = NULL;
HHOOK hMouseHook = NULL;
bool modKeyPressed = false;
bool didUseWindowsKey = false;
bool shouldMinimize = false;
std::chrono::steady_clock::time_point lastClickTime;
NOTIFYICONDATA nid;

static HWND getTopLevelParent(HWND hwnd) {
	HWND parent = hwnd;
	HWND tmp;
	while ((tmp = GetParent(parent)) != NULL) {
		parent = tmp;
	}
	return parent;
}

static void snapToMonitor(HWND window, HMONITOR screen) {
	MONITORINFO info{};
	info.cbSize = sizeof(MONITORINFO);
	GetMonitorInfo(screen, &info);

	RECT rect = info.rcWork; // Work area, excluding taskbars

	HMONITOR currentScreen = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);

	if (currentScreen == screen) {
		return;
	}

	WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
	ShowWindow(window, SW_RESTORE);
	GetWindowPlacement(window, &wp);

	// Calculate the new position (center of the new screen)
	int width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
	int height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
	int x = rect.left + (rect.right - rect.left - width) / 2;
	int y = rect.top + (rect.bottom - rect.top - height) / 2;

	MoveWindow(window, x, y, width, height, TRUE);
	ShowWindow(window, SW_MAXIMIZE);
}

static bool isWindowAllowed(HWND win) {
	LPTSTR className = new TCHAR[256];
	GetClassName(win, className, 256);

	std::string str{ static_cast<std::string>(CT2A(className)) };

	if (disallowedClasses.count(str) > 0) {
		return false;
	}

	return true;
}

template <ResizerCursor cursor>
void SetGlobalCursor() {
	HCURSOR hCursor = NULL;
	switch (cursor) {
	case SIZEALL:
		hCursor = LoadCursor(NULL, IDC_SIZEALL);
		break;
	case SIZENWSE:
		hCursor = LoadCursor(NULL, IDC_SIZENWSE);
		break;
	case SIZENESW:
		hCursor = LoadCursor(NULL, IDC_SIZENESW);
		break;
	default:
		// Leave null to reset to system cursor
		break;
	}

	if (hCursor == NULL) {
		SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, 0);
		return;
	}

	for (auto cursor : systemCursors) {
		HCURSOR newCursor = CopyCursor(hCursor);
		SetSystemCursor(newCursor, cursor);
	}
}

static void AddTrayIcon(HWND hWnd) {
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = TRAY_ICON_UID;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;  // Message for tray icon interaction
	nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);  // Use default application icon
	lstrcpy(nid.szTip, TEXT("Resizer (click to exit)"));  // Tray icon tooltip
	Shell_NotifyIcon(NIM_ADD, &nid);
}

static void RemoveTrayIcon() {
	Shell_NotifyIcon(NIM_DELETE, &nid);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_TASKBARCREATED) {
		AddTrayIcon(hWnd);
		return 0;
	}

	switch (uMsg) {
	case WM_TRAYICON:
		if (lParam == WM_LBUTTONDOWN || lParam == WM_RBUTTONDOWN) {
			// Exit the program when the tray icon is clicked
			PostQuitMessage(0);
		}
		break;

	case WM_DESTROY:
		RemoveTrayIcon();
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

static HMONITOR SysGetMonitorContainingPoint(int x, int y) {
	MonitorSearchData data = { x, y, NULL };

	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&data);

	// If no monitor is found, default to the primary monitor
	if (!data.hMonitor) {
		data.hMonitor = MonitorFromPoint(POINT{ x, y }, MONITOR_DEFAULTTOPRIMARY);
	}

	return data.hMonitor;
}

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

	HWND hWnd = CreateWindowEx(0, CLASS_NAME, L"Resizer", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInstance, NULL);

	if (hWnd == NULL) {
		return 0;
	}

	WM_TASKBARCREATED = RegisterWindowMessage(TEXT("TaskbarCreated"));

	AddTrayIcon(hWnd);

	hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
	hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);

	if (!hKeyboardHook || !hMouseHook) {
		std::cerr << "Failed to install hooks!" << std::endl;
		return 1;
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

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
					didUseWindowsKey = false;
				}
				modKeyPressed = true;
			}
			else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
				modKeyPressed = false;
				if (didUseWindowsKey) {
					// Win + F13 does nothing, ugly workaround which stops the windows menu
					// from appearing after correctly handled event

					INPUT inputs[2] = {};
					ZeroMemory(inputs, sizeof(inputs));

					inputs[0].type = INPUT_KEYBOARD;
					inputs[0].ki.wVk = DUMMY_KEY;

					inputs[1].type = INPUT_KEYBOARD;
					inputs[1].ki.wVk = DUMMY_KEY;
					inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

					UINT uSent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
				}
			}
		}
	}
	return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
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
					adjustWindowOpacity(OPACITY_STEP);
				}
				else if (delta < 0) {
					adjustWindowOpacity(-OPACITY_STEP);
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
		// If we don't do this, weird stuff happens
		case WM_MBUTTONDOWN: {
			if (modKeyPressed) {
				shouldMinimize = true;
				didUseWindowsKey = true;
				return 1;
			}
			break;
		}
		case WM_MBUTTONUP: {
			if (shouldMinimize) {
				shouldMinimize = false;
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

void adjustWindowOpacity(int change) {
	POINT pt;
	GetCursorPos(&pt);
	HWND hwnd = getTopLevelParent(WindowFromPoint(pt));

	if ((hwnd == NULL) || !isWindowAllowed(hwnd)) {
		return;
	}

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

void minimizeWindow() {
	POINT pt;
	GetCursorPos(&pt);
	HWND hwnd = WindowFromPoint(pt);
	hwnd = getTopLevelParent(hwnd);

	if (hwnd == NULL || !isWindowAllowed(hwnd)) {
		return;
	}

	ShowWindow(hwnd, SW_MINIMIZE);
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

	GetCursorPos(&ctx.startMousePos);
	ctx.targetWindow = getTopLevelParent(WindowFromPoint(ctx.startMousePos));

	if (ctx.targetWindow == NULL || !isWindowAllowed(ctx.targetWindow)) {
		ctx.inProgress = false;
		CloseHandle(ctx.hEvent);
		ctx.hEvent = NULL;
		return;
	}

	// Double-click for toggle maximize
	if (ctx.operationType == MOVE) {
		SetGlobalCursor<SIZEALL>();
		auto currentTime = std::chrono::steady_clock::now();
		auto timeSinceLastClick = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastClickTime).count();

		if (timeSinceLastClick <= DOUBLE_CLICK_THRESHOLD) {
			WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
			GetWindowPlacement(ctx.targetWindow, &wp);
			ShowWindow(ctx.targetWindow, wp.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);

			stopWindowOperation();
			return;
		}
		lastClickTime = currentTime;
	}

	GetWindowRect(ctx.targetWindow, &ctx.startWindowRect);
	SetForegroundWindow(ctx.targetWindow);

	HANDLE hThread = CreateThread(NULL, 0, WindowOperationThreadProc, NULL, 0, NULL);
	if (hThread != NULL) {
		CloseHandle(hThread);
	}
}

void stopWindowOperation() {
	ctx.inProgress = false;
	SetGlobalCursor<UNSET>();
	if (ctx.hEvent != NULL) {
		SetEvent(ctx.hEvent);
		CloseHandle(ctx.hEvent);
		ctx.hEvent = NULL;
	}
}

DWORD WINAPI WindowOperationThreadProc(LPVOID lpParam) {
	RECT currentWindowRect = ctx.startWindowRect;

	if (ctx.operationType == MOVE) {
		SetGlobalCursor<SIZEALL>();
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
				snapToMonitor(ctx.targetWindow, screen);
			}
			else {
				MoveWindow(ctx.targetWindow,
					currentWindowRect.left + dx, currentWindowRect.top + dy,
					currentWindowRect.right - currentWindowRect.left,
					currentWindowRect.bottom - currentWindowRect.top, TRUE);
			}

			Sleep(1);
		}
	}
	else if (ctx.operationType == RESIZE) {
		bool isLeft = false, isTop = false;
		int windowWidth = currentWindowRect.right - currentWindowRect.left;
		int windowHeight = currentWindowRect.bottom - currentWindowRect.top;

		// Determine which corner is nearest
		if (ctx.startMousePos.x - currentWindowRect.left < windowWidth / 2)
			isLeft = true;
		if (ctx.startMousePos.y - currentWindowRect.top < windowHeight / 2)
			isTop = true;

		bool nwse = (isLeft && isTop) || (!isLeft && !isTop);

		if (nwse) {
			SetGlobalCursor<SIZENWSE>();
		}
		else {
			SetGlobalCursor<SIZENESW>();
		}

		// Restore window if maximized
		WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
		GetWindowPlacement(ctx.targetWindow, &wp);
		if (wp.showCmd == SW_MAXIMIZE) {
			ShowWindow(ctx.targetWindow, SW_RESTORE);
		}

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

			Sleep(1);
		}
	}

	ctx.inProgress = false;
	return 0;
}
