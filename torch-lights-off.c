/*
 * torch-lights-off - turns off the lighting of a ROCCAT Torch microphone the same way
 * ROCCAT SWARM's "no lighting" preset does. The mic still shows gain and mute while in use.
 *
 *   torch-lights-off.exe           apply once and exit (shows a message if the mic isn't found)
 *   torch-lights-off.exe --quiet   apply once and exit, never shows a message
 *   torch-lights-off.exe --watch   stay in the background and re-apply whenever the mic is
 *                                  plugged in or the PC wakes up (only one copy runs)
 *
 * Build: gcc -O2 -s -municode -mwindows torch-lights-off.c -o torch-lights-off.exe -lsetupapi
 */
#include <windows.h>
#include <setupapi.h>
#include <dbt.h>
#include <wchar.h>

/* HID interface 3 of the Torch (VID 1E7D, PID 3A56): the Consumer Control collection SWARM writes to. */
static const wchar_t TORCH_PATH_ID[] = L"vid_1e7d&pid_3a56&mi_03";
static const GUID HID_INTERFACE_GUID = {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};

/* What SWARM sends for the "no lighting" preset, and the pause it leaves after each packet. */
static const BYTE PACKETS[][5] = {
    {0x08, 0x80, 0x80, 0x00, 0x00}, /* take LED control from the firmware's default (blinking) effect */
    {0x08, 0x80, 0x00, 0x00, 0x00}, /* firmware preset 0: static colour, black */
    {0x08, 0x00, 0x00, 0x00, 0x00}, /* commit */
};
static const DWORD PAUSE_MS[] = {100, 200, 100};

#define REAPPLY_TIMER 1

static BOOL is_torch_path(const wchar_t *path)
{
    wchar_t lower[512];

    lstrcpynW(lower, path, ARRAYSIZE(lower));
    CharLowerW(lower);
    return wcsstr(lower, TORCH_PATH_ID) != NULL;
}

static BOOL write_report(HANDLE dev, const BYTE *report)
{
    OVERLAPPED ov = {0};
    DWORD written = 0;
    BOOL ok;

    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent)
        return FALSE;
    ok = WriteFile(dev, report, 5, NULL, &ov) || GetLastError() == ERROR_IO_PENDING;
    if (ok) {
        if (WaitForSingleObject(ov.hEvent, 1000) != WAIT_OBJECT_0)
            CancelIo(dev);
        ok = GetOverlappedResult(dev, &ov, &written, TRUE) && written == 5;
    }
    CloseHandle(ov.hEvent);
    return ok;
}

static BOOL send_lights_off(const wchar_t *path)
{
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    HANDLE dev = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, share, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    BOOL ok = TRUE;

    if (dev == INVALID_HANDLE_VALUE)
        dev = CreateFileW(path, GENERIC_WRITE, share, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (dev == INVALID_HANDLE_VALUE)
        return FALSE;
    for (int i = 0; i < (int)ARRAYSIZE(PACKETS) && ok; i++) {
        ok = write_report(dev, PACKETS[i]);
        Sleep(PAUSE_MS[i]);
    }
    CloseHandle(dev);
    return ok;
}

/* 1 = lights turned off, 0 = no Torch connected, -1 = found a Torch but couldn't write to it. */
static int lights_off(void)
{
    HDEVINFO set = SetupDiGetClassDevsW(&HID_INTERFACE_GUID, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA iface = {0};
    int result = 0;

    if (set == INVALID_HANDLE_VALUE)
        return 0;
    iface.cbSize = sizeof iface;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &HID_INTERFACE_GUID, i, &iface); i++) {
        union {
            SP_DEVICE_INTERFACE_DETAIL_DATA_W d;
            BYTE raw[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 512 * sizeof(wchar_t)];
        } detail;

        detail.d.cbSize = sizeof detail.d;
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, &detail.d, sizeof detail, NULL, NULL) ||
            !is_torch_path(detail.d.DevicePath))
            continue;
        if (send_lights_off(detail.d.DevicePath))
            result = 1;
        else if (result == 0)
            result = -1;
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

static LRESULT CALLBACK watch_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL) {
            const DEV_BROADCAST_DEVICEINTERFACE_W *dev = (const DEV_BROADCAST_DEVICEINTERFACE_W *)lp;

            /* Wait a moment so the mic has finished starting up; repeated arrivals just restart the timer. */
            if (dev->dbcc_devicetype == DBT_DEVTYP_DEVICEINTERFACE && is_torch_path(dev->dbcc_name))
                SetTimer(hwnd, REAPPLY_TIMER, 1500, NULL);
        }
        return TRUE;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC)
            SetTimer(hwnd, REAPPLY_TIMER, 3000, NULL);
        return TRUE;
    case WM_TIMER:
        KillTimer(hwnd, REAPPLY_TIMER);
        lights_off();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int watch(HINSTANCE inst)
{
    WNDCLASSW wc = {0};
    DEV_BROADCAST_DEVICEINTERFACE_W filter = {0};
    HWND hwnd;
    MSG msg;

    CreateMutexW(NULL, TRUE, L"Local\\torch-lights-off-watch");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    wc.lpfnWndProc = watch_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"torch-lights-off";
    RegisterClassW(&wc);
    /* A hidden top-level window: it gets plug-in notifications and the wake-up broadcast. */
    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"torch-lights-off", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!hwnd)
        return 2;

    filter.dbcc_size = sizeof filter;
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = HID_INTERFACE_GUID;
    RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    lights_off();
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
        DispatchMessageW(&msg);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show)
{
    int result = 0;

    (void)prev;
    (void)show;
    if (wcsstr(args, L"--watch"))
        return watch(inst);

    /* At logon the mic may still be starting up, so give it a few seconds to appear. */
    for (int attempt = 0; attempt < 5 && (result = lights_off()) == 0; attempt++)
        Sleep(1000);

    if (result <= 0 && !wcsstr(args, L"--quiet"))
        MessageBoxW(NULL,
                    result == 0 ? L"No ROCCAT Torch found. Is it plugged in?"
                                : L"Found the ROCCAT Torch, but couldn't send the lights-off command to it.",
                    L"torch-lights-off", MB_ICONWARNING);
    return result > 0 ? 0 : (result == 0 ? 1 : 2);
}
