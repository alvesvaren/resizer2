#include "window_ops.h"

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
    UINT resizeFlags = SWP_NOZORDER | SWP_NOSENDCHANGING | SWP_DEFERERASE;
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

void snapToFancyZone(HWND hWnd, HMONITOR hMon, POINT mousePos, bool maximized)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hMon, &mi)) return;

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
        adjustRect(hWnd, newRect);
        moveWindow<RESIZE>(hWnd, newRect);
    }
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


