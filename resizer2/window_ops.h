#pragma once

#include "resizer2.h"
#include <WinUser.h>
#include <shcore.h>

// Cursor helpers
extern const std::array<int, 13> systemCursors;
template <ResizerCursor cursor>
void SetGlobalCursor();

// Window helpers
HWND getTopLevelParent(HWND hwnd);
bool isWindowAllowed(HWND win);
RECT getWindowFrameBounds(HWND hWnd);
void snapToMonitor(HWND window, HMONITOR screen);
void snapToFancyZone(HWND hWnd, HMONITOR hMon, POINT mousePos, bool maximized);

template <ContextType type>
void moveWindow(HWND hwnd, RECT rect, bool async = true);


