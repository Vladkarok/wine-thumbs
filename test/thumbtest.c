/*
 * Test driver for the file dialog thumbnail view.
 *
 * Opens GetOpenFileNameW the way Last War does (explorer style, no hook,
 * read-only checkbox visible) or the Vista IFileOpenDialog, optionally sends
 * a view command to the dialog, then saves a capture of the dialog to PNG.
 *
 * usage: thumbtest.exe [--itemdlg] <initial dir> <out dir> <actions>
 */

#define COBJMACROS
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <initguid.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef GR_GDIOBJECTS
#define GR_GDIOBJECTS 0
#define GR_USEROBJECTS 1
#endif
#ifndef CAPTUREBLT
#define CAPTUREBLT 0x40000000
#endif

static DWORD main_tid;
static const WCHAR *out_path;

static BOOL CALLBACK find_dialog_proc(HWND hwnd, LPARAM lparam)
{
    WCHAR cls[64];

    GetClassNameW(hwnd, cls, ARRAY_SIZE(cls));
    if (!wcscmp(cls, L"#32770") && IsWindowVisible(hwnd))
    {
        *(HWND *)lparam = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HRESULT save_png(const WCHAR *path, const BYTE *bgrx, UINT width, UINT height)
{
    IWICImagingFactory *factory;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IWICStream *stream = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    UINT stride = (width * 3 + 3) & ~3u, x, y;
    BYTE *rgb;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr)) return hr;

    rgb = malloc(stride * height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            memcpy(rgb + y * stride + x * 3, bgrx + (y * width + x) * 4, 3);

    if (SUCCEEDED(hr = IWICImagingFactory_CreateStream(factory, &stream))
            && SUCCEEDED(hr = IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE))
            && SUCCEEDED(hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder))
            && SUCCEEDED(hr = IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache))
            && SUCCEEDED(hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL))
            && SUCCEEDED(hr = IWICBitmapFrameEncode_Initialize(frame, NULL))
            && SUCCEEDED(hr = IWICBitmapFrameEncode_SetSize(frame, width, height))
            && SUCCEEDED(hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format))
            && SUCCEEDED(hr = IWICBitmapFrameEncode_WritePixels(frame, height, stride, stride * height, rgb))
            && SUCCEEDED(hr = IWICBitmapFrameEncode_Commit(frame)))
        hr = IWICBitmapEncoder_Commit(encoder);

    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IWICStream_Release(stream);
    IWICImagingFactory_Release(factory);
    free(rgb);
    return hr;
}

static void capture(HWND dlg)
{
    BITMAPINFO bmi = {{sizeof(bmi.bmiHeader)}};
    HDC screen_dc, mem_dc;
    HBITMAP dib, old;
    void *bits;
    RECT rc;
    HRESULT hr;

    GetWindowRect(dlg, &rc);
    bmi.bmiHeader.biWidth = rc.right - rc.left;
    bmi.bmiHeader.biHeight = -(rc.bottom - rc.top);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;

    screen_dc = GetWindowDC(dlg);
    mem_dc = CreateCompatibleDC(screen_dc);
    dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mem_dc, dib);
    BitBlt(mem_dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, screen_dc, 0, 0, SRCCOPY | CAPTUREBLT);
    GdiFlush();

    hr = save_png(out_path, bits, rc.right - rc.left, rc.bottom - rc.top);
    printf("capture %ldx%ld -> %#lx\n", rc.right - rc.left, rc.bottom - rc.top, hr);

    SelectObject(mem_dc, old);
    DeleteObject(dib);
    DeleteDC(mem_dc);
    ReleaseDC(dlg, screen_dc);
}

static const WCHAR *actions;
static const WCHAR *out_dir;

static BOOL CALLBACK find_child_proc(HWND hwnd, LPARAM lparam)
{
    const WCHAR **cls_and_result = (const WCHAR **)lparam;
    WCHAR cls[64];

    GetClassNameW(hwnd, cls, ARRAY_SIZE(cls));
    if (!wcscmp(cls, cls_and_result[0]))
    {
        cls_and_result[1] = (const WCHAR *)hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND find_child(HWND parent, const WCHAR *cls)
{
    const WCHAR *data[2] = {cls, NULL};

    EnumChildWindows(parent, find_child_proc, (LPARAM)data);
    return (HWND)data[1];
}

/* actions: comma separated list of
 *   cmd:<id>      WM_COMMAND to the dialog
 *   viewcmd:<id>  WM_COMMAND to the shell view window
 *   wait:<ms>
 *   shot:<name>   capture the dialog to <outdir>/<name>.png
 *   select:<n>    focus the list and select item n
 *   key:<vk>      key press in the list
 *   scroll:<n>    scroll the list by n lines
 *   close         cancel the dialog
 */
static int repeat = 1;

static void run_actions(void);

static DWORD WINAPI driver_thread(void *arg)
{
    int n;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    for (n = 0; n < repeat; n++) run_actions();
    return 0;
}

static void run_actions(void)
{
    static WCHAR buf[4096], path[MAX_PATH];
    WCHAR *action, *next, *param;
    HWND dlg = NULL, list, prev = NULL;
    int i;

    for (i = 0; i < 100 && !dlg; i++)
    {
        Sleep(100);
        EnumThreadWindows(main_tid, find_dialog_proc, (LPARAM)&dlg);
    }
    if (!dlg)
    {
        printf("dialog not found\n");
        ExitProcess(2);
    }
    Sleep(500);

    wcscpy(buf, actions);
    for (action = buf; action && *action; action = next)
    {
        if ((next = wcschr(action, ','))) *next++ = 0;
        if ((param = wcschr(action, ':'))) *param++ = 0;
        list = find_child(dlg, L"SysListView32");

        if (!wcscmp(action, L"cmd"))
            SendMessageW(dlg, WM_COMMAND, wcstoul(param, NULL, 0), 0);
        else if (!wcscmp(action, L"viewcmd"))
            SendMessageW(find_child(dlg, L"SHELLDLL_DefView"), WM_COMMAND, wcstoul(param, NULL, 0), 0);
        else if (!wcscmp(action, L"wait"))
            Sleep(wcstoul(param, NULL, 0));
        else if (!wcscmp(action, L"shot"))
        {
            swprintf(path, ARRAY_SIZE(path), L"%s\\%s.png", out_dir, param);
            out_path = path;
            capture(dlg);
        }
        else if (!wcscmp(action, L"select") && list)
        {
            LVITEMW item = {0};

            SendMessageW(dlg, WM_NEXTDLGCTL, (WPARAM)list, TRUE);
            item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(list, LVM_SETITEMSTATE, -1, (LPARAM)&item);
            item.state = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(list, LVM_SETITEMSTATE, wcstoul(param, NULL, 0), (LPARAM)&item);
            SendMessageW(list, LVM_ENSUREVISIBLE, wcstoul(param, NULL, 0), FALSE);
        }
        else if (!wcscmp(action, L"key") && list)
        {
            PostMessageW(list, WM_KEYDOWN, wcstoul(param, NULL, 0), 0);
            PostMessageW(list, WM_KEYUP, wcstoul(param, NULL, 0), 0xc0000000);
        }
        else if (!wcscmp(action, L"scroll") && list)
        {
            int n = wcstol(param, NULL, 0);

            /* posted like real input, so the dialog loop paints right after */
            while (n-- > 0) PostMessageW(list, WM_VSCROLL, SB_LINEDOWN, 0);
        }
        else if (!wcscmp(action, L"tbstate"))
        {
            HWND tb = find_child(dlg, L"ToolbarWindow32"), first = GetDlgItem(dlg, 1);
            WCHAR cls[64];

            GetClassNameW(first, cls, ARRAY_SIZE(cls));
            printf("GetDlgItem(1) class %ls\n", cls);
            for (tb = FindWindowExW(dlg, NULL, L"ToolbarWindow32", NULL); tb; tb = FindWindowExW(dlg, tb, L"ToolbarWindow32", NULL))
                if (SendMessageW(tb, TB_COMMANDTOINDEX, 0xa001, 0) != -1) break;
            UINT ids[] = {0xa003, 0xa004, 0xa006};

            for (i = 0; i < ARRAY_SIZE(ids); i++)
                printf("toolbar %#x state %#Ix\n", ids[i], SendMessageW(tb, TB_GETSTATE, ids[i], 0));
        }
        else if (!wcscmp(action, L"strings"))
        {
            WCHAR text[128];
            HMENU menu, sub;
            int k;

            char utf8[512];

            LoadStringW(GetModuleHandleW(L"comdlg32.dll"), 155, text, ARRAY_SIZE(text));
            WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
            printf("comdlg32 tooltip: %s\n", utf8);
            menu = LoadMenuW(GetModuleHandleW(L"shell32.dll"), L"MENU_002");
            sub = GetSubMenu(GetSubMenu(menu, 0), 0);
            for (k = 0; k < GetMenuItemCount(sub); k++)
            {
                GetMenuStringW(sub, k, text, ARRAY_SIZE(text), MF_BYPOSITION);
                WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
                printf("shell32 view menu: %s\n", utf8);
            }
            DestroyMenu(menu);
        }
        else if (!wcscmp(action, L"close"))
            PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
        else
            printf("unknown action %ls\n", action);
        fflush(stdout);
    }
    /* wait until this dialog is gone before looking for the next one */
    for (i = 0; i < 100 && IsWindow(dlg); i++) Sleep(50);
    (void)prev;
}

static void run_getopenfilename(const WCHAR *dir)
{
    WCHAR file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};

    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAY_SIZE(file);
    ofn.lpstrInitialDir = dir;
    ofn.lpstrTitle = L"Select a Picture";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    printf("GetOpenFileNameW -> %d\n", GetOpenFileNameW(&ofn));
}

static void run_itemdlg(const WCHAR *dir)
{
    static const COMDLG_FILTERSPEC filters[] =
    {
        {L"Image Files", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif"},
        {L"All Files", L"*.*"},
    };
    IFileOpenDialog *fod;
    IShellItem *folder;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&fod);
    if (FAILED(hr)) { printf("CoCreateInstance %#lx\n", hr); return; }
    IFileOpenDialog_SetFileTypes(fod, ARRAY_SIZE(filters), filters);
    if (SUCCEEDED(SHCreateItemFromParsingName(dir, NULL, &IID_IShellItem, (void **)&folder)))
    {
        IFileOpenDialog_SetFolder(fod, folder);
        IShellItem_Release(folder);
    }
    hr = IFileOpenDialog_Show(fod, NULL);
    printf("IFileOpenDialog::Show -> %#lx\n", hr);
    IFileOpenDialog_Release(fod);
}

int wmain(int argc, WCHAR **argv)
{
    BOOL itemdlg = FALSE;
    int arg = 1, i;

    for (;;)
    {
        if (argc > arg && !wcscmp(argv[arg], L"--itemdlg")) itemdlg = TRUE;
        else if (argc > arg && !wcscmp(argv[arg], L"--dpiaware")) SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
        else break;
        arg++;
    }
    if (argc - arg < 3)
    {
        printf("usage: thumbtest.exe [--itemdlg] <initial dir> <out dir> <actions>\n");
        return 1;
    }
    out_dir = argv[arg + 1];
    actions = argv[arg + 2];

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    main_tid = GetCurrentThreadId();
    CloseHandle(CreateThread(NULL, 0, driver_thread, NULL, 0, NULL));

    if (argc - arg > 3) repeat = wcstoul(argv[arg + 3], NULL, 0);
    for (i = 0; i < repeat; i++)
    {
        if (itemdlg) run_itemdlg(argv[arg]);
        else run_getopenfilename(argv[arg]);
        printf("round %d: gdi objects %lu, user objects %lu\n", i, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
               GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
        fflush(stdout);
    }

    CoUninitialize();
    return 0;
}
