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
	notifyInput();
}

enum class ScreenEdge { None, Outer, Inner };

// How close to a shared edge between monitors the cursor has to be to snap there, and how far it
// can then move away before the snap is dropped, at 96 DPI
constexpr int INNER_EDGE_BAND = 16;
constexpr int INNER_EDGE_RELEASE_BAND = 64;
// How long the cursor has to stay near a shared edge to snap there, so a window dragged across to
// the other monitor does not snap on its way
constexpr ULONGLONG INNER_EDGE_DWELL_MS = 150;

// Which kind of monitor edge the cursor is at: pushed against one with no monitor beyond it, or
// near one shared with another monitor, which the cursor would otherwise just cross
static ScreenEdge getScreenEdge(POINT pt, int innerBand) {
	MONITORINFO mi{};
	mi.cbSize = sizeof(mi);
	GetMonitorInfo(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
	RECT& screen = mi.rcMonitor;
	LONG band = MulDiv(innerBand, GetDpiForSystem(), 96);

	struct Side { LONG distance; POINT beyond; };
	Side sides[] = {
		{ pt.x - screen.left, { screen.left - 1, pt.y } },
		{ screen.right - 1 - pt.x, { screen.right, pt.y } },
		{ pt.y - screen.top, { pt.x, screen.top - 1 } },
		{ screen.bottom - 1 - pt.y, { pt.x, screen.bottom } },
	};
	ScreenEdge edge = ScreenEdge::None;
	for (auto& [distance, beyond] : sides) {
		bool shared = MonitorFromPoint(beyond, MONITOR_DEFAULTTONULL) != NULL;
		if (!shared && distance <= 0) return ScreenEdge::Outer;
		if (shared && distance < band) edge = ScreenEdge::Inner;
	}
	return edge;
}

DWORD WINAPI WindowOperationThreadProc(LPVOID lpParam) {
	RECT currentWindowRect = ctx.startWindowRect;

	if (ctx.operationType == MOVE) {
		SetGlobalCursor<SIZEALL>();
		WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
		POINT pt;
		bool wasInZoneMode = false;
		bool snappedToZone = false;
		bool dragStarted = false;
		// Like Windows, pushing the window against a screen edge snaps it, but only once the drag has
		// been away from the edges, so a drag that starts there (such as on a maximized window's
		// title bar) does not snap right away
		bool leftScreenEdges = false;
		ULONGLONG innerEdgeSince = 0;
		bool snappedAtInnerEdge = false;

		// Every placement of the window goes through here: with "Show window contents while
		// dragging" off, an outline moves instead and the window takes its last place at the end
		std::optional<DragOutline> outline;
		std::optional<WindowPositioner> positioner;
		if (showWindowContentsWhileDragging()) {
			positioner.emplace(ctx.targetWindow);
		}
		else {
			outline.emplace(ctx.targetWindow);
		}
		std::optional<RECT> placed;
		auto place = [&](RECT rect, ContextType type) {
			placed = rect;
			if (outline) {
				outline->move(rect);
			}
			else {
				positioner->request(rect, type);
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

			// Holding Shift, pushing against a screen edge or resting near an edge shared with another
			// monitor snaps to zones
			ScreenEdge edge = getScreenEdge(pt, snappedAtInnerEdge ? INNER_EDGE_RELEASE_BAND : INNER_EDGE_BAND);
			ULONGLONG now = GetTickCount64();
			if (edge == ScreenEdge::None) leftScreenEdges = true;
			if (edge != ScreenEdge::Inner) innerEdgeSince = 0;
			else if (!innerEdgeSince) innerEdgeSince = now;
			bool atSnapEdge = leftScreenEdges &&
				(edge == ScreenEdge::Outer || (edge == ScreenEdge::Inner && now - innerEdgeSince >= INNER_EDGE_DWELL_MS));
			snappedAtInnerEdge = atSnapEdge && edge == ScreenEdge::Inner;
			bool zoneMode = (GetAsyncKeyState(VK_SHIFT) & 0x8000) || atSnapEdge;

			// In zone mode, a maximized window is restored first
			if (zoneMode) {
				wasInZoneMode = true;
				// If window currently maximized, un-maximize it so we can do a "normal" snap
				if (maximized) {
					// Update our tracking rect to the newly-restored size
					GetWindowRect(ctx.targetWindow, &currentWindowRect);
				}

				HMONITOR screen = SysGetMonitorContainingPoint(pt.x, pt.y);
				std::optional<RECT> zoneRect = getFancyZoneRect(ctx.targetWindow, screen, pt, maximized);
				snappedToZone = zoneRect.has_value();
				if (zoneRect) place(*zoneRect, RESIZE);
			}
			else {
				if (wasInZoneMode) {
					wasInZoneMode = false;
					snappedToZone = false;
					RECT newRect = ctx.startWindowRect;
					OffsetRect(&newRect, dx, dy);
					place(newRect, RESIZE);
				}
				if (maximized) {
					// Option A: Just re-snap to the monitor so user can
					// "drag" the window across monitors even while maximized
					HMONITOR screen = SysGetMonitorContainingPoint(pt.x, pt.y);
					snapToMonitor(ctx.targetWindow, screen);
				}
				else if (dragStarted) {
					RECT rect = currentWindowRect;
					rect.left += dx;
					rect.right += dx;
					rect.top += dy;
					rect.bottom += dy;
					place(rect, MOVE);
				}
			}

			pumpMessages();
			waitForInput();
		}

		positioner.reset();
		if (outline) {
			outline.reset();
			if (placed) moveWindow<RESIZE>(ctx.targetWindow, *placed, false);
		}
		if (snappedToZone) markWindowArranged(ctx.targetWindow);
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
			waitForInput();
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


