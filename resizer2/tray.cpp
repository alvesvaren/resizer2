#include "resizer2.h"

static bool IsExplorerReady() {
    return FindWindow(L"Shell_TrayWnd", NULL) != NULL;
}

void AddTrayIcon(HWND hWnd) {
    if (g_trayIconAdded) {
        return;
    }
    if (!IsExplorerReady()) {
        return;
    }

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = TRAY_ICON_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP
#ifdef NIF_SHOWTIP
        | NIF_SHOWTIP
#endif
        ;
    nid.uCallbackMessage = WM_TRAYICON;
    HICON smallIcon = (HICON)LoadImage(NULL, IDI_APPLICATION, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    nid.hIcon = smallIcon ? smallIcon : LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(nid.szTip, TEXT("Resizer"));
    BOOL addOk = Shell_NotifyIcon(NIM_ADD, &nid);
    if (addOk) {
        g_trayIconAdded = true;
    }
}

void RemoveTrayIcon() {
    if (g_trayIconAdded) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        g_trayIconAdded = false;
    }
}

void ShowTrayMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, TEXT("Exit"));
    SetForegroundWindow(hWnd);
    UINT cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, NULL);
    SendMessage(hWnd, WM_NULL, 0, 0);
    if (cmd == ID_TRAY_EXIT) {
        PostQuitMessage(0);
    }
    DestroyMenu(hMenu);
}


