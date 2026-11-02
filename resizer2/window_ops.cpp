#include "window_ops.h"
#include <mutex>
#include <optional>
#include <set>

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

bool isWindowAllowed(HWND win) {
    WCHAR className[256] = {0};
    GetClassNameW(win, className, 256);
    std::wstring cls(className);
    return disallowedClasses.count(cls) == 0;
}

HWND getTopLevelParent(HWND hwnd) {
    HWND parent = hwnd;
    HWND tmp;
    while ((tmp = GetParent(parent)) != NULL) {
        parent = tmp;
    }
    return parent;
}

template <ContextType type>
void moveWindow(HWND hwnd, RECT rect, bool async) {
    UINT resizeFlags = SWP_NOZORDER | SWP_NOSENDCHANGING | SWP_DEFERERASE | SWP_NOACTIVATE;
    UINT moveFlags =  resizeFlags | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOCOPYBITS;
    SetWindowPos(hwnd, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, (type == MOVE ? moveFlags : resizeFlags) | (async ? SWP_ASYNCWINDOWPOS : 0));
}

// Explicit instantiations
template void moveWindow<MOVE>(HWND, RECT, bool);
template void moveWindow<RESIZE>(HWND, RECT, bool);

void adjustRect(HWND win, RECT& rect) {
    LONG style = GetWindowLong(win, GWL_STYLE);
    LONG styleEx = GetWindowLong(win, GWL_EXSTYLE);
    AdjustWindowRectEx(&rect, style, FALSE, styleEx);
}

// With per-monitor DPI awareness, coordinates from User32/DWM are already in device pixels.
// No additional manual scaling should be applied.

RECT getWindowFrameBounds(HWND hWnd) {
  RECT bounds{};
  HRESULT hr = DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds,
                                     sizeof(bounds));
  if (SUCCEEDED(hr)) {
    return bounds;
  }

  POINT mousePos = {0, 0};
  GetCursorPos(&mousePos);
  GetWindowRect(hWnd, &bounds);
  
  return bounds;
}

void snapToMonitor(HWND window, HMONITOR screen) {
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

    int width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    int height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;

    int x = rect.left + (rect.right - rect.left - width) / 2;
    int y = rect.top + (rect.bottom - rect.top - height) / 2;

    MoveWindow(window, x, y, width, height, TRUE);
    ShowWindow(window, SW_MAXIMIZE);
}

typedef BOOL(WINAPI* IsWindowArranged_t)(HWND);
static const IsWindowArranged_t isWindowArranged =
    (IsWindowArranged_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowArranged");

// A frame edge within this many pixels of a work area edge counts as touching it
constexpr int EDGE_TOLERANCE = 8;

static bool touches(LONG edge, LONG workEdge) {
    return abs(edge - workEdge) <= EDGE_TOLERANCE;
}

struct SnappedWindow {
    HWND hwnd;
    RECT frame;
};

// Snapped windows shown on the given monitor, topmost first
static std::vector<SnappedWindow> getSnappedWindows(HMONITOR monitor, HWND exclude) {
    struct Search { HMONITOR monitor; HWND exclude; std::vector<SnappedWindow> found; };
    Search search{ monitor, exclude };
    if (!isWindowArranged) return search.found;

    EnumWindows([](HWND other, LPARAM param) -> BOOL {
        auto& [monitor, exclude, found] = *(Search*)param;
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(other, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (other != exclude && IsWindowVisible(other) && !IsIconic(other) && !cloaked && isWindowArranged(other) &&
            MonitorFromWindow(other, MONITOR_DEFAULTTONEAREST) == monitor) {
            found.push_back({ other, getWindowFrameBounds(other) });
        }
        return TRUE;
    }, (LPARAM)&search);
    return search.found;
}

static MONITORINFO getMonitorInfo(HWND hWnd) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi);
    return mi;
}

// When the other side of the screen already holds a snapped window, Windows snaps a new window
// into the space next to it instead of the exact half. Predict that, so the window does not jump
// when the zone is snapped natively.
static void fitZoneToSnappedNeighbour(HWND hWnd, const RECT& work, RECT& zone) {
    if (zone.right - zone.left >= work.right - work.left) return;

    for (auto& [other, frame] : getSnappedWindows(MonitorFromRect(&work, MONITOR_DEFAULTTONEAREST), hWnd)) {
        if (frame.top >= zone.bottom || frame.bottom <= zone.top) continue;
        if (zone.left == work.left && touches(frame.right, work.right) && frame.left > zone.left) {
            zone.right = frame.left;
            return;
        }
        if (zone.right == work.right && touches(frame.left, work.left) && frame.right < zone.right) {
            zone.left = frame.right;
            return;
        }
    }
}

bool snapToFancyZone(HWND hWnd, HMONITOR hMon, POINT mousePos, bool maximized)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hMon, &mi)) return false;

    int monWidth = mi.rcWork.right - mi.rcWork.left;
    int monHeight = mi.rcWork.bottom - mi.rcWork.top;

    POINT tlCorner = { mi.rcWork.left,  mi.rcWork.top };
    POINT trCorner = { mi.rcWork.right, mi.rcWork.top };
    POINT blCorner = { mi.rcWork.left,  mi.rcWork.bottom };
    POINT brCorner = { mi.rcWork.right, mi.rcWork.bottom };

    double radius = CORNER_RADIUS_FRACTION * min(monWidth, monHeight);

    auto dist = [](POINT a, POINT b) {
        double dx = double(a.x - b.x);
        double dy = double(a.y - b.y);
        return sqrt(dx * dx + dy * dy);
    };

    double distTL = dist(mousePos, tlCorner);
    double distTR = dist(mousePos, trCorner);
    double distBL = dist(mousePos, blCorner);
    double distBR = dist(mousePos, brCorner);

    int centerX = mi.rcWork.left + monWidth / 2;
    int centerY = mi.rcWork.top + monHeight / 2;

    if (maximized) {
        ShowWindow(hWnd, SW_RESTORE);
    }

    RECT newRect = mi.rcWork;
    bool shouldMove = false;

    if (distTL <= radius) {
        newRect.right -= monWidth / 2;
        newRect.bottom -= monHeight / 2;
        shouldMove = true;
    }
    else if (distTR <= radius) {
        newRect.left += monWidth / 2;
        newRect.bottom -= monHeight / 2;
        shouldMove = true;
    }
    else if (distBL <= radius) {
        newRect.right -= monWidth / 2;
        newRect.top += monHeight / 2;
        shouldMove = true;
    }
    else if (distBR <= radius) {
        newRect.left += monWidth / 2;
        newRect.top += monHeight / 2;
        shouldMove = true;
    }
    else {
        int oneThirdY = mi.rcWork.top + (monHeight / 3);
        int twoThirdY = mi.rcWork.top + 2 * (monHeight / 3);

        if ((mousePos.y > oneThirdY) && (mousePos.y < twoThirdY))
        {
            if (mousePos.x < centerX) {
                newRect.right -= monWidth / 2;
            }
            else {
                newRect.left += monWidth / 2;
            }
            shouldMove = true;
        }

        int oneThirdX = mi.rcWork.left + (monWidth / 3);
        int twoThirdX = mi.rcWork.left + 2 * (monWidth / 3);

        if ((mousePos.x > oneThirdX) && (mousePos.x < twoThirdX))
        {
            if (mousePos.y < centerY) {
                newRect.bottom -= monHeight / 2;
            }
            else {
                newRect.top += monHeight / 2;
            }
            shouldMove = true;
        }
    }

    if (shouldMove) {
        fitZoneToSnappedNeighbour(hWnd, mi.rcWork, newRect);

        // Expand to include DWM frame so the visible frame fits the zone exactly
        RECT currentWindowRect{};
        GetWindowRect(hWnd, &currentWindowRect);
        RECT currentFrameRect = getWindowFrameBounds(hWnd);

        int leftMargin = currentFrameRect.left - currentWindowRect.left;
        int topMargin = currentFrameRect.top - currentWindowRect.top;
        int rightMargin = currentWindowRect.right - currentFrameRect.right;
        int bottomMargin = currentWindowRect.bottom - currentFrameRect.bottom;

        RECT targetOuter{};
        targetOuter.left = newRect.left - leftMargin;
        targetOuter.top = newRect.top - topMargin;
        targetOuter.right = newRect.right + rightMargin;
        targetOuter.bottom = newRect.bottom + bottomMargin;

        moveWindow<RESIZE>(hWnd, targetOuter);
    }
    return shouldMove;
}

static void sendKey(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &in, sizeof(INPUT));
}

static bool isKeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// Windows only lets a background process take the foreground in some cases; attaching to the
// current foreground thread's input lets SetForegroundWindow succeed in more of them
static bool forceForegroundWindow(HWND hWnd) {
    HWND foreground = GetForegroundWindow();
    if (foreground == hWnd) return true;

    DWORD foregroundThread = GetWindowThreadProcessId(foreground, NULL);
    DWORD currentThread = GetCurrentThreadId();
    bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
        AttachThreadInput(currentThread, foregroundThread, TRUE);
    SetForegroundWindow(hWnd);
    BringWindowToTop(hWnd);
    if (attached) AttachThreadInput(currentThread, foregroundThread, FALSE);

    for (int elapsed = 0; elapsed < 100 && GetForegroundWindow() != hWnd; elapsed += 5) {
        Sleep(5);
    }
    return GetForegroundWindow() == hWnd;
}

// Which part of its monitor's work area a window covers, judged from which work area edges its
// visible frame touches. A side that spans the whole work area has no position, so left/top are
// false there and zones compare with plain equality.
struct Zone {
    bool halfWidth, halfHeight, left, top;
    bool operator==(const Zone&) const = default;
};

static Zone getZone(HWND hWnd) {
    RECT frame = getWindowFrameBounds(hWnd);
    RECT work = getMonitorInfo(hWnd).rcWork;
    bool touchesLeft = touches(frame.left, work.left);
    bool touchesTop = touches(frame.top, work.top);
    bool halfWidth = !(touchesLeft && touches(frame.right, work.right));
    bool halfHeight = !(touchesTop && touches(frame.bottom, work.bottom));
    return { halfWidth, halfHeight, halfWidth && touchesLeft, halfHeight && touchesTop };
}

static bool waitForZone(HWND hWnd, Zone expected) {
    for (int elapsed = 0; elapsed < 500; elapsed += 5) {
        Sleep(5);
        if (isWindowArranged(hWnd) && getZone(hWnd) == expected) return true;
    }
    return false;
}

// Windows only treats a window as snapped ("arranged") when the shell snapped it itself, and only
// then does it get shared-edge resizing with its neighbours. There is no API to set that state, so
// recreate the zone with the Win+arrow shortcuts, as one Win press: from an unsnapped window,
// Left/Right snaps to that half and a following Up/Down to that quarter. Top/bottom halves have no
// shortcut on landscape monitors, so those are snapped to the left half and moved back; the
// arranged state survives the move.
static void markWindowArranged(HWND hWnd) {
    Zone target = getZone(hWnd);
    if (!target.halfWidth && !target.halfHeight) return;
    bool left = !target.halfWidth || target.left;
    RECT targetRect{};
    GetWindowRect(hWnd, &targetRect);

    // Win+arrow acts on the foreground window, so nothing is sent unless that is our window
    if (!forceForegroundWindow(hWnd)) return;

    // Start from an unsnapped window, where the shortcuts always have the same effect
    if (isWindowArranged(hWnd)) ShowWindow(hWnd, SW_RESTORE);

    std::vector<std::pair<WORD, Zone>> steps = {
        { left ? VK_LEFT : VK_RIGHT, Zone{ true, false, left, false } },
    };
    if (target.halfWidth && target.halfHeight) steps.push_back({ target.top ? VK_UP : VK_DOWN, target });

    // The window is already where it should end up, so hide the shell's snap animation
    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));

    bool snapped = true;
    sendKey(VK_LWIN, false);
    for (auto [arrow, expected] : steps) {
        sendKey(arrow, false);
        sendKey(arrow, true);
        snapped = waitForZone(hWnd, expected);
        if (!snapped) break;
    }
    sendKey(VK_LWIN, true);

    if (snapped && !target.halfWidth) {
        moveWindow<RESIZE>(hWnd, targetRect, false);
    }

    disableTransitions = FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));
}

// Windows whose last drop was into a zone. They are marked once Win and Shift are released: the
// shortcuts then get a Win press of their own, which held keys (Win+Shift+arrow moves windows to
// another monitor) and the user's own Win press cannot interfere with.
static std::mutex windowsToMarkMutex;
static std::set<HWND> windowsToMark;
static bool markerRunning = false;

static DWORD WINAPI MarkWindowsThreadProc(LPVOID) {
    for (;;) {
        while (isKeyDown(VK_LWIN) || isKeyDown(VK_RWIN) || isKeyDown(VK_SHIFT)) Sleep(10);

        std::set<HWND> windows;
        {
            std::lock_guard lock(windowsToMarkMutex);
            if (windowsToMark.empty()) {
                markerRunning = false;
                return 0;
            }
            windows.swap(windowsToMark);
        }
        for (HWND hWnd : windows) {
            if (IsWindow(hWnd) && !(ctx.inProgress && ctx.targetWindow == hWnd)) markWindowArranged(hWnd);
        }
    }
}

void setWindowSnappedToZone(HWND hWnd, bool snapped) {
    if (!isWindowArranged) return;

    std::lock_guard lock(windowsToMarkMutex);
    if (!snapped) {
        windowsToMark.erase(hWnd);
        return;
    }
    windowsToMark.insert(hWnd);
    if (markerRunning) return;

    HANDLE thread = CreateThread(NULL, 0, MarkWindowsThreadProc, NULL, 0, NULL);
    if (thread) {
        markerRunning = true;
        CloseHandle(thread);
    }
}

// Resizing a snapped window works like dragging the lines between snapped windows: only edges
// inside the work area move, and every snapped window with an edge on the same line moves that
// edge along, so a divider through a whole grid of windows moves as one and every window stays
// snapped. Edges along the work area border stay put, since moving them would unsnap the window.
// An unsnapped window is alone on its lines and both grabbed edges move.
std::vector<ResizeParticipant> getResizeParticipants(HWND hWnd, bool isLeft, bool isTop) {
    RECT start{};
    GetWindowRect(hWnd, &start);
    LONG RECT::* xEdge = isLeft ? &RECT::left : &RECT::right;
    LONG RECT::* yEdge = isTop ? &RECT::top : &RECT::bottom;
    if (!isWindowArranged || !isWindowArranged(hWnd)) return { { hWnd, start, xEdge, yEdge } };

    RECT frame = getWindowFrameBounds(hWnd);
    RECT work = getMonitorInfo(hWnd).rcWork;
    std::optional<LONG> xLine = touches(frame.*xEdge, work.*xEdge) ? std::nullopt : std::optional(frame.*xEdge);
    std::optional<LONG> yLine = touches(frame.*yEdge, work.*yEdge) ? std::nullopt : std::optional(frame.*yEdge);

    // The edge of a frame that lies on the line, if any
    auto edgeOnLine = [](const RECT& frame, std::optional<LONG> line, LONG RECT::* first, LONG RECT::* second) -> LONG RECT::* {
        if (!line) return nullptr;
        if (touches(frame.*first, *line)) return first;
        if (touches(frame.*second, *line)) return second;
        return nullptr;
    };

    std::vector<ResizeParticipant> participants = {
        { hWnd, start, xLine ? xEdge : nullptr, yLine ? yEdge : nullptr },
    };
    for (auto& [other, otherFrame] : getSnappedWindows(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), hWnd)) {
        LONG RECT::* otherX = edgeOnLine(otherFrame, xLine, &RECT::left, &RECT::right);
        LONG RECT::* otherY = edgeOnLine(otherFrame, yLine, &RECT::top, &RECT::bottom);
        if (!otherX && !otherY) continue;

        RECT otherStart{};
        GetWindowRect(other, &otherStart);
        participants.push_back({ other, otherStart, otherX, otherY });
    }
    return participants;
}

// Like a native drag: moving a snapped window un-snaps it and gives back its pre-snap size, with the
// cursor kept at the same relative spot on the window. Updates rect to the restored window's position.
bool unsnapWindow(HWND hWnd, POINT grab, RECT& rect) {
    if (!isWindowArranged || !isWindowArranged(hWnd)) return false;

    RECT before{};
    GetWindowRect(hWnd, &before);
    ShowWindow(hWnd, SW_RESTORE);
    RECT after{};
    GetWindowRect(hWnd, &after);

    int width = after.right - after.left;
    int height = after.bottom - after.top;
    double grabFraction = double(grab.x - before.left) / max(1, before.right - before.left);
    int grabOffsetY = min(max(0, (int)(grab.y - before.top)), height - 1);

    rect.left = grab.x - (int)(grabFraction * width);
    rect.top = grab.y - grabOffsetY;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    moveWindow<RESIZE>(hWnd, rect, false);
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
        break;
    }

    if (hCursor == NULL) {
        SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, 0);
        return;
    }

    for (auto cursorId : systemCursors) {
        HCURSOR newCursor = CopyCursor(hCursor);
        SetSystemCursor(newCursor, cursorId);
    }
}

// Explicit instantiations
template void SetGlobalCursor<SIZEALL>();
template void SetGlobalCursor<SIZENWSE>();
template void SetGlobalCursor<SIZENESW>();
template void SetGlobalCursor<UNSET>();


