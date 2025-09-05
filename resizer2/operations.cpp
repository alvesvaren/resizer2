#include "resizer2.h"
#include "window_ops.h"

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

// Explicit instantiations
template void startWindowOperation<MOVE>();
template void startWindowOperation<RESIZE>();

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
		POINT pt;
		bool wasShiftDown = false;

		while (ctx.inProgress) {
			if (WaitForSingleObject(ctx.hEvent, 0) == WAIT_OBJECT_0) {
				break;
			}

			GetWindowPlacement(ctx.targetWindow, &wp);
			bool maximized = (wp.showCmd == SW_MAXIMIZE);

			GetCursorPos(&pt);

			int dx = pt.x - ctx.startMousePos.x;
			int dy = pt.y - ctx.startMousePos.y;

			// Check SHIFT
			bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

			// If SHIFT is down, we want to force-restore a maximized window
			if (shiftDown) {
				wasShiftDown = true;
				// If window currently maximized, un-maximize it so we can do a "normal" snap
				if (maximized) {
					// Update our tracking rect to the newly-restored size
					GetWindowRect(ctx.targetWindow, &currentWindowRect);
				}

				// SHIFT is pressed => do fancy zones
				HMONITOR screen = SysGetMonitorContainingPoint(pt.x, pt.y);
				snapToFancyZone(ctx.targetWindow, screen, pt, maximized);
			}
			else {
				if (wasShiftDown) {
					wasShiftDown = false;
					RECT newRect = ctx.startWindowRect;
					OffsetRect(&newRect, dx, dy);
					moveWindow<RESIZE>(ctx.targetWindow, newRect);

				}
				// SHIFT is not pressed:
				if (maximized) {
					// Option A: Just re-snap to the monitor so user can
					// "drag" the window across monitors even while maximized
					HMONITOR screen = SysGetMonitorContainingPoint(pt.x, pt.y);
					snapToMonitor(ctx.targetWindow, screen);
				}
				else {
					RECT rect = currentWindowRect;
					rect.left += dx;
					rect.right += dx;
					rect.top += dy;
					rect.bottom += dy;
					moveWindow<MOVE>(ctx.targetWindow, rect);
				}
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

			moveWindow<RESIZE>(ctx.targetWindow, newRect, false);

			Sleep(1);
		}
	}

	ctx.inProgress = false;
	return 0;
}


