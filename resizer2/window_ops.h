#pragma once

#include "resizer2.h"
#include <WinUser.h>
#include <shcore.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

// Cursor helpers
extern const std::array<int, 13> systemCursors;
template <ResizerCursor cursor>
void SetGlobalCursor();

// Window helpers
HWND getTopLevelParent(HWND hwnd);
bool isWindowAllowed(HWND win);
RECT getWindowFrameBounds(HWND hWnd);
void snapToMonitor(HWND window, HMONITOR screen);
bool snapToFancyZone(HWND hWnd, HMONITOR hMon, POINT mousePos, bool maximized);
void setWindowSnappedToZone(HWND hWnd, bool snapped);
bool unsnapWindow(HWND hWnd, POINT grab, RECT& rect);
std::vector<ResizeParticipant> getResizeParticipants(HWND hWnd, LONG RECT::* xEdge, LONG RECT::* yEdge);

template <ContextType type>
void moveWindow(HWND hwnd, RECT rect, bool async = true);

// Waits for the next composed frame, so window updates go out once per displayed frame
void waitForNextFrame();

// Applies the latest requested size and position to a window from a thread of its own.
// SetWindowPos on another process's window waits until that app has handled the change, so a
// slow app only delays its own updates, and requests made meanwhile collapse into the newest one
// instead of queueing up behind it.
class WindowPositioner {
public:
    explicit WindowPositioner(HWND hwnd);
    ~WindowPositioner(); // Applies the last request, then stops
    WindowPositioner(const WindowPositioner&) = delete;
    WindowPositioner& operator=(const WindowPositioner&) = delete;

    void request(RECT rect);

private:
    void run();

    HWND hwnd;
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<RECT> pending;
    bool stopping = false;
    std::thread thread;
};


