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
#include <cderr.h>
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


static void dump_menu(HMENU menu, int depth)
{
    int i, count = GetMenuItemCount(menu);

    for (i = 0; i < count; i++)
    {
        MENUITEMINFOW mii = {sizeof(mii)};
        WCHAR text[128] = L"";
        char utf8[256];

        mii.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE | MIIM_STATE | MIIM_STRING;
        mii.dwTypeData = text;
        mii.cch = ARRAY_SIZE(text);
        GetMenuItemInfoW(menu, i, TRUE, &mii);
        WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
        printf("menu %*s%s id=%#x%s%s\n", depth * 2, "", (mii.fType & MFT_SEPARATOR) ? "----" : utf8, mii.wID,
               (mii.fState & MFS_CHECKED) ? " [x]" : "", (mii.fType & MFT_RADIOCHECK) ? " (radio)" : "");
        if (mii.hSubMenu) dump_menu(mii.hSubMenu, depth + 1);
    }
}

static HWND find_menu_window(void)
{
    HWND hwnd = NULL;
    int i;

    for (i = 0; i < 30 && !hwnd; i++)
    {
        Sleep(100);
        hwnd = FindWindowW(L"#32768", NULL);
    }
    return hwnd;
}

/* actions: comma separated list of
 *   cmd:<id>      WM_COMMAND to the dialog
 *   viewcmd:<id>  WM_COMMAND to the shell view window
 *   wait:<ms>
 *   shot:<name>   capture the dialog to <outdir>/<name>.png
 *   select:<n>    focus the list and select item n
 *   key:<vk>      key press in the list
 *   scroll:<n>    scroll the list by n lines
 *   ctxmenu       open the view background menu, dump it, pick nothing
 *   menucmd:<id>  run a background menu command the way a click does
 *   items:<n>     print the first n item labels of the list
 *   type:<text>   set the file name box
 *   ok            press Open/Save
 *   address:<p>   type a path in the item dialog address box and press Enter
 *   click:<id>    WM_COMMAND to the dialog
 *   addsel:<n>    add item n to the selection
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
        else if (!wcscmp(action, L"ctxmenu"))
        {
            HWND view = find_child(dlg, L"SHELLDLL_DefView"), menu_hwnd;
            RECT rc;

            GetWindowRect(view, &rc);
            PostMessageW(view, WM_CONTEXTMENU, (WPARAM)view, MAKELPARAM(rc.right - 20, rc.bottom - 20));
            if ((menu_hwnd = find_menu_window()))
            {
                HMENU menu = (HMENU)SendMessageW(menu_hwnd, MN_GETHMENU, 0, 0);
                UINT pick = param ? wcstoul(param, NULL, 0) : 0;

                dump_menu(menu, 0);
                if (pick)
                {
                    /* walk to the item with the keyboard like a user would */
                    int path[4], depth = 0, i, j, count;
                    HMENU cur = menu;
                    BOOL found = FALSE;

                    for (i = 0; i < GetMenuItemCount(cur) && !found; i++)
                    {
                        if (GetMenuItemID(cur, i) == pick) { path[depth++] = i; found = TRUE; break; }
                        if (GetSubMenu(cur, i))
                        {
                            HMENU sub = GetSubMenu(cur, i);
                            count = GetMenuItemCount(sub);
                            for (j = 0; j < count; j++)
                                if (GetMenuItemID(sub, j) == pick) { path[0] = i; path[1] = j; depth = 2; found = TRUE; break; }
                        }
                    }
                    printf("pick %#x: %s\n", pick, found ? "found" : "NOT FOUND");
                    if (found)
                    {
                        HMENU level = menu;
                        HWND target = menu_hwnd;

                        for (i = 0; i < depth; i++)
                        {
                            int k, pos = 0;
                            /* count selectable items before the target position */
                            for (k = 0; k < path[i]; k++)
                            {
                                UINT st = GetMenuState(level, k, MF_BYPOSITION);
                                if (!(st & MF_SEPARATOR)) pos++;
                            }
                            for (k = 0; k <= pos; k++) { PostMessageW(target, WM_KEYDOWN, VK_DOWN, 0); Sleep(30); }
                            if (i + 1 < depth)
                            {
                                PostMessageW(target, WM_KEYDOWN, VK_RIGHT, 0);
                                Sleep(300);
                                level = GetSubMenu(level, path[i]);
                                target = FindWindowExW(NULL, menu_hwnd, L"#32768", NULL);
                                if (!target) target = menu_hwnd;
                            }
                        }
                        PostMessageW(target, WM_KEYDOWN, VK_RETURN, 0);
                    }
                }
                if (!pick) PostMessageW(menu_hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            }
            else printf("no context menu\n");
        }
        else if (!wcscmp(action, L"menucmd"))
        {
            HWND view_hwnd = find_child(dlg, L"SHELLDLL_DefView");
            IShellBrowser *browser = (IShellBrowser *)SendMessageW(GetParent(view_hwnd), WM_USER + 7, 0, 0);
            IShellView *view = NULL;
            IContextMenu *cm = NULL;
            HMENU menu = CreatePopupMenu();
            HRESULT hr = E_FAIL;

            if (!browser) browser = (IShellBrowser *)SendMessageW(dlg, WM_USER + 7, 0, 0);
            if (browser && SUCCEEDED(IShellBrowser_QueryActiveShellView(browser, &view))
                    && SUCCEEDED(IShellView_GetItemObject(view, SVGIO_BACKGROUND, &IID_IContextMenu, (void **)&cm)))
            {
                CMINVOKECOMMANDINFO info = {sizeof(info)};
                UINT id = wcstoul(param, NULL, 0);

                IContextMenu_QueryContextMenu(cm, menu, 0, 0, 0x7fff, 0);
                info.hwnd = GetParent(view_hwnd);
                info.lpVerb = MAKEINTRESOURCEA(id);
                hr = IContextMenu_InvokeCommand(cm, &info);
            }
            printf("menucmd %ls -> %#lx\n", param, hr);
            if (cm) IContextMenu_Release(cm);
            if (view) IShellView_Release(view);
            DestroyMenu(menu);
        }
        else if (!wcscmp(action, L"items") && list)
        {
            int n = wcstoul(param, NULL, 0), k;
            WCHAR text[260];
            char utf8[520];

            for (k = 0; k < n; k++)
            {
                LVITEMW item = {0};

                item.iSubItem = 0;
                item.pszText = text;
                item.cchTextMax = ARRAY_SIZE(text);
                text[0] = 0;
                if (!SendMessageW(list, LVM_GETITEMTEXTW, k, (LPARAM)&item)) break;
                WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
                printf("item %d: %s\n", k, utf8);
            }
        }
        else if (!wcscmp(action, L"type"))
        {
            if (!SetDlgItemTextW(dlg, 0x480 /* edt1 */, param)) SetDlgItemTextW(dlg, 0x47c /* cmb13 */, param);
        }
        else if (!wcscmp(action, L"address"))
        {
            HWND edit = GetDlgItem(dlg, 204 /* IDC_NAVADDRESS */);

            SetWindowTextW(edit, param);
            SendMessageW(dlg, WM_NEXTDLGCTL, (WPARAM)edit, TRUE);
            Sleep(100);
            PostMessageW(dlg, WM_COMMAND, IDOK, 0);
        }
        else if (!wcscmp(action, L"tbmenu"))
        {
            /* tbmenu:<toolbar control id>:<button command id>; click it, dump the popup */
            WCHAR *cmd = wcschr(param, ':');
            HWND tb = GetDlgItem(dlg, wcstoul(param, NULL, 0)), menu_hwnd;
            RECT rc;

            if (tb && cmd && SendMessageW(tb, TB_GETRECT, wcstoul(cmd + 1, NULL, 0), (LPARAM)&rc))
            {
                LPARAM pos = MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);

                PostMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, pos);
                PostMessageW(tb, WM_LBUTTONUP, 0, pos);
                if ((menu_hwnd = find_menu_window()))
                {
                    dump_menu((HMENU)SendMessageW(menu_hwnd, MN_GETHMENU, 0, 0), 0);
                    PostMessageW(menu_hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
                }
                else printf("no popup\n");
            }
            else printf("no toolbar button\n");
        }
        else if (!wcscmp(action, L"resize"))
        {
            WCHAR *h = wcschr(param, 'x');

            SetWindowPos(dlg, NULL, 0, 0, wcstoul(param, NULL, 0), h ? wcstoul(h + 1, NULL, 0) : 600,
                         SWP_NOMOVE | SWP_NOZORDER);
        }
        else if (!wcscmp(action, L"click"))
            PostMessageW(dlg, WM_COMMAND, wcstoul(param, NULL, 0), 0);
        else if (!wcscmp(action, L"ok"))
            PostMessageW(dlg, WM_COMMAND, IDOK, 0);
        else if (!wcscmp(action, L"addsel") && list)
        {
            LVITEMW item = {0};

            item.stateMask = item.state = LVIS_SELECTED;
            SendMessageW(list, LVM_SETITEMSTATE, wcstoul(param, NULL, 0), (LPARAM)&item);
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

static struct
{
    BOOL save, ansi;
    DWORD flags;
    const WCHAR *defext, *file;
    DWORD maxfile;
} ofn_opts = { FALSE, FALSE, OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST, NULL, NULL, MAX_PATH };

static void print_utf8(const char *label, const WCHAR *text)
{
    char utf8[1024];

    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    printf("%s%s\n", label, utf8);
}

static void run_getopenfilename(const WCHAR *dir)
{
    static const WCHAR filter[] = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All Files\0*.*\0";
    WCHAR *file = calloc(ofn_opts.maxfile + 2, sizeof(WCHAR)), title[MAX_PATH] = L"", cwd[MAX_PATH];
    OPENFILENAMEW ofn = {sizeof(ofn)};
    const WCHAR *p;
    BOOL ret;

    if (ofn_opts.file) lstrcpynW(file, ofn_opts.file, ofn_opts.maxfile);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = ofn_opts.maxfile;
    ofn.lpstrFileTitle = title;
    ofn.nMaxFileTitle = ARRAY_SIZE(title);
    ofn.lpstrInitialDir = dir;
    ofn.lpstrTitle = ofn_opts.save ? NULL : L"Select a Picture";
    ofn.lpstrDefExt = ofn_opts.defext;
    ofn.Flags = ofn_opts.flags;

    if (ofn_opts.ansi)
    {
        OPENFILENAMEA ofna = {sizeof(ofna)};
        char *filea = calloc(ofn_opts.maxfile + 2, 1), titlea[MAX_PATH] = "", dira[MAX_PATH], defexta[16];
        static const char filtera[] = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All Files\0*.*\0";

        WideCharToMultiByte(CP_ACP, 0, dir, -1, dira, sizeof(dira), NULL, NULL);
        ofna.lpstrFilter = filtera;
        ofna.lpstrFile = filea;
        ofna.nMaxFile = ofn_opts.maxfile;
        ofna.lpstrFileTitle = titlea;
        ofna.nMaxFileTitle = sizeof(titlea);
        ofna.lpstrInitialDir = dira;
        if (ofn_opts.defext)
        {
            WideCharToMultiByte(CP_ACP, 0, ofn_opts.defext, -1, defexta, sizeof(defexta), NULL, NULL);
            ofna.lpstrDefExt = defexta;
        }
        ofna.Flags = ofn_opts.flags;
        ret = ofn_opts.save ? GetSaveFileNameA(&ofna) : GetOpenFileNameA(&ofna);
        printf("GetXxxFileNameA -> %d, error %#lx, offset %u, ext %u, filter %lu, flags %#lx\n", ret,
               CommDlgExtendedError(), ofna.nFileOffset, ofna.nFileExtension, ofna.nFilterIndex, ofna.Flags);
        if (ret)
        {
            const char *q;
            for (q = filea; *q; q += strlen(q) + 1) printf("  file: %s\n", q);
            printf("  title: %s\n", titlea);
        }
        free(filea);
        return;
    }

    ret = ofn_opts.save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    printf("GetXxxFileNameW -> %d, error %#lx, offset %u, ext %u, filter %lu, flags %#lx\n", ret,
           CommDlgExtendedError(), ofn.nFileOffset, ofn.nFileExtension, ofn.nFilterIndex, ofn.Flags);
    if (ret)
    {
        for (p = file; *p; p += lstrlenW(p) + 1) print_utf8("  file: ", p);
        print_utf8("  title: ", title);
    }
    else if (CommDlgExtendedError() == FNERR_BUFFERTOOSMALL)
        printf("  needed %u\n", *(WORD *)file);
    GetCurrentDirectoryW(ARRAY_SIZE(cwd), cwd);
    print_utf8("  cwd: ", cwd);
    free(file);
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

    for (; argc > arg && !wcsncmp(argv[arg], L"--", 2); arg++)
    {
        const WCHAR *opt = argv[arg] + 2, *val = wcschr(opt, '=');

        if (val) val++;
        if (!wcscmp(opt, L"itemdlg")) itemdlg = TRUE;
        else if (!wcscmp(opt, L"dpiaware")) SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
        else if (!wcscmp(opt, L"save")) ofn_opts.save = TRUE;
        else if (!wcscmp(opt, L"ansi")) ofn_opts.ansi = TRUE;
        else if (!wcscmp(opt, L"multi")) ofn_opts.flags |= OFN_ALLOWMULTISELECT;
        else if (!wcsncmp(opt, L"flags=", 6)) ofn_opts.flags = wcstoul(val, NULL, 0);
        else if (!wcsncmp(opt, L"defext=", 7)) ofn_opts.defext = val;
        else if (!wcsncmp(opt, L"file=", 5)) ofn_opts.file = val;
        else if (!wcsncmp(opt, L"maxfile=", 8)) ofn_opts.maxfile = wcstoul(val, NULL, 0);
        else printf("unknown option %ls\n", argv[arg]);
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
