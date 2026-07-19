#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include "lumen_bridge.h"

namespace {
LumenVolume* volume = nullptr;
int axisIndex[3] = {0, 0, 0};
float level = 40.0f, window = 400.0f;
HWND statusBar = nullptr;

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), out.data(), n, nullptr, nullptr);
    return out;
}

void setStatus(const std::wstring& text) { if (statusBar) SetWindowTextW(statusBar, text.c_str()); }

void openFolder(HWND hwnd) {
    BROWSEINFOW info{};
    info.hwndOwner = hwnd;
    info.lpszTitle = L"Select a DICOM folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&info);
    if (!pidl) return;
    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    char msg[512]{};
    LumenVolume* loaded = lumen_load_folder(utf8(path).c_str(), msg, sizeof(msg));
    if (!loaded) {
        MessageBoxA(hwnd, msg[0] ? msg : "Could not load the DICOM folder.", "LumenSlice", MB_ICONERROR);
        return;
    }
    lumen_free(volume);
    volume = loaded;
    axisIndex[0] = axisIndex[1] = axisIndex[2] = 0;
    int w = 0, h = 0, d = 0;
    lumen_dims(volume, &w, &h, &d);
    wchar_t status[160];
    swprintf_s(status, L"Loaded %d x %d x %d   |   Mouse wheel: scroll axial slices   |   +/-: window", w, h, d);
    setStatus(status);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void drawSlice(HDC dc, RECT area, int axis, int index, const wchar_t* title) {
    FillRect(dc, &area, (HBRUSH)(COLOR_WINDOW + 1));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(45, 45, 55));
    TextOutW(dc, area.left + 10, area.top + 8, title, lstrlenW(title));
    if (!volume) return;
    int w = 0, h = 0;
    const unsigned char* pixels = lumen_extract_slice(volume, axis, index, level, window, &w, &h);
    if (!pixels || w <= 0 || h <= 0) return;
    std::vector<unsigned char> rgba(pixels, pixels + (size_t)w * (size_t)h * 4);
    // The bridge owns the mask buffer; retrieve its dimensions so the overlay
    // remains aligned even for non-axial planes.
    const unsigned char* mask;
    int maskW = 0, maskH = 0;
    mask = lumen_extract_mask_slice(volume, axis, index, &maskW, &maskH);
    if (mask && maskW == w && maskH == h) {
        for (int i = 0; i < w * h; ++i) {
            const unsigned char alpha = mask[i * 4 + 3];
            if (!alpha) continue;
            const int base = i * 4;
            rgba[base + 0] = (unsigned char)((rgba[base + 0] * (255 - alpha) + mask[base + 0] * alpha) / 255);
            rgba[base + 1] = (unsigned char)((rgba[base + 1] * (255 - alpha) + mask[base + 1] * alpha) / 255);
            rgba[base + 2] = (unsigned char)((rgba[base + 2] * (255 - alpha) + mask[base + 2] * alpha) / 255);
        }
    }
    RECT image = area;
    image.top += 30;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, image.left, image.top, image.right - image.left, image.bottom - image.top,
                  0, 0, w, h, rgba.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"Open a DICOM folder to begin.",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) openFolder(hwnd);
        if (volume && LOWORD(wParam) == 2) {
            lumen_seg_push_undo(volume);
            lumen_seg_threshold(volume, level - window / 2.0f, level + window / 2.0f);
            setStatus(L"Threshold applied to the active segment.");
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (volume && LOWORD(wParam) == 3) {
            lumen_seg_push_undo(volume); lumen_seg_clear(volume);
            setStatus(L"Active segment cleared."); InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (volume && LOWORD(wParam) == 4) {
            lumen_seg_undo(volume); setStatus(L"Undo"); InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (volume && LOWORD(wParam) == 5) {
            lumen_seg_redo(volume); setStatus(L"Redo"); InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (volume) {
            int count = lumen_slice_count(volume, LUMEN_AXIS_AXIAL);
            axisIndex[0] = std::clamp(axisIndex[0] + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1), 0, std::max(0, count - 1));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_OEM_PLUS || wParam == VK_ADD) window = std::min(4000.0f, window * 1.1f);
        if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) window = std::max(1.0f, window / 1.1f);
        if (wParam == 'O') openFolder(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        RECT client{}; GetClientRect(hwnd, &client);
        client.bottom -= 24;
        int gap = 8, width = (client.right - 4 * gap) / 3;
        RECT a{gap, gap, gap + width, client.bottom - gap};
        RECT b{2 * gap + width, gap, 2 * gap + 2 * width, client.bottom - gap};
        RECT c{3 * gap + 2 * width, gap, 3 * gap + 3 * width, client.bottom - gap};
        drawSlice(dc, a, LUMEN_AXIS_AXIAL, axisIndex[0], L"Axial");
        drawSlice(dc, b, LUMEN_AXIS_CORONAL, axisIndex[1], L"Coronal");
        drawSlice(dc, c, LUMEN_AXIS_SAGITTAL, axisIndex[2], L"Sagittal");
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_SIZE:
        if (statusBar) SendMessageW(statusBar, WM_SIZE, 0, 0);
        return 0;
    case WM_DESTROY: lumen_free(volume); volume = nullptr; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    char executable[MAX_PATH]{};
    GetModuleFileNameA(nullptr, executable, MAX_PATH);
    std::string resourceDir(executable);
    const size_t slash = resourceDir.find_last_of("\\/");
    if (slash != std::string::npos) resourceDir.resize(slash);
    resourceDir += "\\resources\\dicom.dic";
    if (GetFileAttributesA(resourceDir.c_str()) != INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableA("DCMDICTPATH", resourceDir.c_str());
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    const wchar_t* klass = L"LumenSliceWindow";
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = windowProc;
    wc.lpszClassName = klass; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(klass, L"LumenSlice", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr, instance, nullptr);
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu(); AppendMenuW(file, MF_STRING, 1, L"Open DICOM Folder..."); AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"File");
    HMENU segment = CreatePopupMenu();
    AppendMenuW(segment, MF_STRING, 2, L"Threshold active segment");
    AppendMenuW(segment, MF_STRING, 3, L"Clear active segment");
    AppendMenuW(segment, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(segment, MF_STRING, 4, L"Undo"); AppendMenuW(segment, MF_STRING, 5, L"Redo");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)segment, L"Segment");
    SetMenu(hwnd, menu);
    ShowWindow(hwnd, show); UpdateWindow(hwnd);
    MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    CoUninitialize(); return (int)message.wParam;
}
