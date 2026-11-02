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

	// Focus the window
	SetForegroundWindow(ctx.targetWindow);

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
		bool snappedToZone = false;
		bool dragStarted = false;

		// Every placement of the window goes through here: with "Show window contents while
		// dragging" off, an outline moves instead and the window takes its last place at the end
		std::optional<DragOutline> outline;
		if (!showWindowContentsWhileDragging()) outline.emplace(ctx.targetWindow);
		std::optional<RECT> placed;
		auto place = [&](RECT rect, ContextType type) {
			placed = rect;
			if (outline) {
				outline->move(rect);
			}
			else if (type == MOVE) {
				moveWindow<MOVE>(ctx.targetWindow, rect);
			}
			else {
				moveWindow<RESIZE>(ctx.targetWindow, rect);
			}
		};

		while (ctx.inProgress) {
			if (WaitForSingleObject(ctx.hEvent, 0) == WAIT_OBJECT_0) {
				break;
			}

			GetWindowPlacement(ctx.targetWindow, &wp);
			bool maximized = (wp.showCmd == SW_MAXIMIZE);

			GetCursorPos(&pt);

			int dx = pt.x - ctx.startMousePos.x;
			int dy = pt.y - ctx.startMousePos.y;

			if (!dragStarted && (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG))) {
				dragStarted = true;
				if (unsnapWindow(ctx.targetWindow, ctx.startMousePos, currentWindowRect)) {
					ctx.startWindowRect = currentWindowRect;
				}
			}

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
				std::optional<RECT> zoneRect = getFancyZoneRect(ctx.targetWindow, screen, pt, maximized);
				snappedToZone = zoneRect.has_value();
				if (zoneRect) place(*zoneRect, RESIZE);
			}
			else {
				if (wasShiftDown) {
					wasShiftDown = false;
					snappedToZone = false;
					RECT newRect = ctx.startWindowRect;
					OffsetRect(&newRect, dx, dy);
					place(newRect, RESIZE);
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
					place(rect, MOVE);
				}
			}

			pumpMessages();
			waitForNextFrame();
		}

		if (outline) {
			outline.reset();
			if (placed) moveWindow<RESIZE>(ctx.targetWindow, *placed, false);
		}
		setWindowSnappedToZone(ctx.targetWindow, snappedToZone);
	}

	else if (ctx.operationType == RESIZE) {
		// Each axis is split into thirds: grabbing an outer third moves that edge, the middle third
		// leaves the axis alone. Grabbing the very middle resizes along whichever axis the drag
		// starts in, from the nearest edge on that axis.
		RECT grabRect = currentWindowRect;
		auto outerEdge = [](LONG grab, LONG low, LONG high, LONG RECT::* lowEdge, LONG RECT::* highEdge) -> LONG RECT::* {
			LONG third = (high - low) / 3;
			if (grab < low + third) return lowEdge;
			if (grab >= high - third) return highEdge;
			return nullptr;
		};
		auto nearestEdge = [](LONG grab, LONG low, LONG high, LONG RECT::* lowEdge, LONG RECT::* highEdge) {
			return grab < (low + high) / 2 ? lowEdge : highEdge;
		};
		LONG RECT::* xEdge = outerEdge(ctx.startMousePos.x, grabRect.left, grabRect.right, &RECT::left, &RECT::right);
		LONG RECT::* yEdge = outerEdge(ctx.startMousePos.y, grabRect.top, grabRect.bottom, &RECT::top, &RECT::bottom);

		// Restore window if maximized
		WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
		GetWindowPlacement(ctx.targetWindow, &wp);
		if (wp.showCmd == SW_MAXIMIZE) {
			ShowWindow(ctx.targetWindow, SW_RESTORE);
		}

		// With "Show window contents while dragging" off, outlines move instead and the windows
		// take their last places at the end
		bool showContents = showWindowContentsWhileDragging();
		std::vector<ResizeParticipant> participants;
		std::vector<std::unique_ptr<WindowPositioner>> positioners;
		std::vector<std::unique_ptr<DragOutline>> outlines;
		std::vector<RECT> placed;
		auto beginResize = [&] {
			if (!yEdge) {
				SetGlobalCursor<SIZEWE>();
			}
			else if (!xEdge) {
				SetGlobalCursor<SIZENS>();
			}
			else if ((xEdge == &RECT::left) == (yEdge == &RECT::top)) {
				SetGlobalCursor<SIZENWSE>();
			}
			else {
				SetGlobalCursor<SIZENESW>();
			}

			participants = getResizeParticipants(ctx.targetWindow, xEdge, yEdge);
			for (auto& participant : participants) {
				if (showContents) {
					positioners.push_back(std::make_unique<WindowPositioner>(participant.hwnd));
				}
				else {
					outlines.push_back(std::make_unique<DragOutline>(participant.hwnd));
				}
				placed.push_back(participant.start);
			}
		};

		bool resizing = xEdge || yEdge;
		if (resizing) {
			beginResize();
		}
		else {
			SetGlobalCursor<SIZEALL>();
		}

		POINT lastPt = ctx.startMousePos;
		while (ctx.inProgress) {
			if (WaitForSingleObject(ctx.hEvent, 0) == WAIT_OBJECT_0) {
				break;
			}
			POINT pt;
			GetCursorPos(&pt);
			int dx = pt.x - ctx.startMousePos.x;
			int dy = pt.y - ctx.startMousePos.y;

			if (!resizing && (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG))) {
				if (abs(dx) >= abs(dy)) {
					xEdge = nearestEdge(ctx.startMousePos.x, grabRect.left, grabRect.right, &RECT::left, &RECT::right);
				}
				else {
					yEdge = nearestEdge(ctx.startMousePos.y, grabRect.top, grabRect.bottom, &RECT::top, &RECT::bottom);
				}
				resizing = true;
				beginResize();
			}

			if (resizing && (pt.x != lastPt.x || pt.y != lastPt.y)) {
				lastPt = pt;
				for (size_t i = 0; i < participants.size(); i++) {
					auto& [hwnd, start, movingX, movingY] = participants[i];
					RECT newRect = start;
					if (movingX) newRect.*movingX += dx;
					if (movingY) newRect.*movingY += dy;
					placed[i] = newRect;
					if (showContents) {
						positioners[i]->request(newRect);
					}
					else {
						outlines[i]->move(newRect);
					}
				}
			}

			pumpMessages();
			waitForNextFrame();
		}

		if (!showContents) {
			outlines.clear();
			for (size_t i = 0; i < participants.size(); i++) {
				moveWindow<RESIZE>(participants[i].hwnd, placed[i], false);
			}
		}
	}

	ctx.inProgress = false;
	return 0;
}


