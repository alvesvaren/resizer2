#include "resizer2.h"
#include "window_ops.h"
#include <atomic>
#include <mutex>

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

static DWORD lastWinDownTick = 0;

// Physical key state, from the hook's own view of input, so it is known while input is held back
static std::atomic<bool> physicalKeys[256];

// Physical keyboard input held back while resizer sends shortcuts of its own
static std::atomic<bool> capturingKeyboard = false;
static std::mutex capturedMutex;
static std::vector<KBDLLHOOKSTRUCT> captured;

// Marks replayed input, which stands in for physical input
constexpr ULONG_PTR REPLAYED_INPUT = 0x72657332;

static const WORD MODIFIER_KEYS[] = { VK_LWIN, VK_RWIN, VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU };
static bool modifiersHeldAtCapture[ARRAYSIZE(MODIFIER_KEYS)];

static INPUT keyInput(WORD vk, bool up, ULONG_PTR extraInfo = 0) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    in.ki.dwExtraInfo = extraInfo;
    return in;
}

// A modifier released with no other key since it went down counts as a tap (Win opens Start, Alt
// focuses the menu bar), so a key that does nothing is tapped around modifier changes
static void addDummyTap(std::vector<INPUT>& inputs) {
    inputs.push_back(keyInput(DUMMY_KEY, false));
    inputs.push_back(keyInput(DUMMY_KEY, true));
}

void beginKeyboardCapture() {
    capturingKeyboard = true;

    std::vector<INPUT> inputs;
    addDummyTap(inputs);
    for (size_t i = 0; i < ARRAYSIZE(MODIFIER_KEYS); i++) {
        modifiersHeldAtCapture[i] = physicalKeys[MODIFIER_KEYS[i]];
        if (GetAsyncKeyState(MODIFIER_KEYS[i]) & 0x8000) inputs.push_back(keyInput(MODIFIER_KEYS[i], true));
    }
    SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
}

void endKeyboardCapture() {
    std::vector<INPUT> inputs;
    for (size_t i = 0; i < ARRAYSIZE(MODIFIER_KEYS); i++) {
        if (modifiersHeldAtCapture[i]) inputs.push_back(keyInput(MODIFIER_KEYS[i], false, REPLAYED_INPUT));
    }
    if (!inputs.empty()) addDummyTap(inputs);
    SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));

    // Replay in order, including input that arrives while replaying
    for (;;) {
        std::vector<KBDLLHOOKSTRUCT> events;
        {
            std::lock_guard lock(capturedMutex);
            if (captured.empty()) {
                capturingKeyboard = false;
                return;
            }
            events.swap(captured);
        }

        std::vector<INPUT> replay;
        for (auto& event : events) {
            INPUT in = keyInput((WORD)event.vkCode, event.flags & LLKHF_UP, REPLAYED_INPUT);
            in.ki.wScan = (WORD)event.scanCode;
            if (event.flags & LLKHF_EXTENDED) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            replay.push_back(in);
        }
        SendInput((UINT)replay.size(), replay.data(), sizeof(INPUT));
    }
}

// Keep modifier state in sync with either Win key being held, in case a key-up was missed.
// The async key state is only updated after all low-level hooks have run, so right after a
// Win key-down it still reads as up; trust the hook event until key repeat would have kicked in.
static void syncModKeyState() {
    if (modKeyPressed &&
        GetTickCount() - lastWinDownTick > 1000 &&
        (GetAsyncKeyState(VK_LWIN) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_RWIN) & 0x8000) == 0) {
        modKeyPressed = false;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* pKbDllHookStruct = (KBDLLHOOKSTRUCT*)lParam;
    if (nCode == HC_ACTION) {
        syncModKeyState();
        if (ctx.inProgress) notifyInput();

        bool injected = pKbDllHookStruct->flags & LLKHF_INJECTED;
        if (!injected && capturingKeyboard) {
            std::lock_guard lock(capturedMutex);
            captured.push_back(*pKbDllHookStruct);
            return 1;
        }
        if ((!injected || pKbDllHookStruct->dwExtraInfo == REPLAYED_INPUT) && pKbDllHookStruct->vkCode < 256) {
            physicalKeys[pKbDllHookStruct->vkCode] = !(pKbDllHookStruct->flags & LLKHF_UP);
        }

        if (pKbDllHookStruct->vkCode == VK_LWIN || pKbDllHookStruct->vkCode == VK_RWIN) {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (!modKeyPressed) {
                    didUseWindowsKey = false;
                }
                modKeyPressed = true;
                lastWinDownTick = GetTickCount();
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
        syncModKeyState();
        if (ctx.inProgress) notifyInput();

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


