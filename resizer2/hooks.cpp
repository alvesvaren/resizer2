#include "resizer2.h"
#include "window_ops.h"

void EnsureHooksInstalled() {
    if (g_hooksInstalled) return;
    HHOOK kb = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    HHOOK mouse = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    if (kb && mouse) {
        hKeyboardHook = kb;
        hMouseHook = mouse;
        g_hooksInstalled = true;
    }
    else {
        if (kb) UnhookWindowsHookEx(kb);
        if (mouse) UnhookWindowsHookEx(mouse);
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* pKbDllHookStruct = (KBDLLHOOKSTRUCT*)lParam;
    if (nCode == HC_ACTION) {
        if (modKeyPressed && (GetAsyncKeyState(VK_LWIN) & 0x8000) == 0) {
            modKeyPressed = false;
        }

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
                    INPUT inputs[2] = {};
                    ZeroMemory(inputs, sizeof(inputs));

                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = DUMMY_KEY;

                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = DUMMY_KEY;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

                    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
                }
            }
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    MSLLHOOKSTRUCT* pMsLlHookStruct = (MSLLHOOKSTRUCT*)lParam;
    if (nCode == HC_ACTION) {
        if (modKeyPressed && (GetAsyncKeyState(VK_LWIN) & 0x8000) == 0) {
            modKeyPressed = false;
        }

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
                return 1;
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


