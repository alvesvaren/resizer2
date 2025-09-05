#pragma once

#include "resizer2.h"

// Cursor helpers
extern const std::array<int, 13> systemCursors;
template <ResizerCursor cursor>
void SetGlobalCursor();

// Window helpers
HWND getTopLevelParent(HWND hwnd);
bool isWindowAllowed(HWND win);
void adjustRect(HWND win, RECT& rect);
RECT getWindowFrameBounds(HWND hWnd);
void snapToMonitor(HWND window, HMONITOR screen);
void snapToFancyZone(HWND hWnd, HMONITOR hMon, POINT mousePos, bool maximized);

template <ContextType type>
void moveWindow(HWND hwnd, RECT rect, bool async = true);


