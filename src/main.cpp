// 程序入口
#include "common.h"
#include "platform/dpi.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "ui/hotkeys.h"
#include "ui/list_view.h"
#include "ui/reminder.h"
#include "ui/row_editor.h"
#include "ui/context.h"
#include "app/store.h"
#include "ui/theme.h"
#include "ui/toast.h"
#include "ui/main_window.h"
#include "ui/autostart.h"


// ---------------------------- 入口 ------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    // v2.3.2：注册表自启项带 --autostart —— 开机时只驻留托盘，不弹主窗口
    const std::wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
    const bool startHidden = cmdLine.find(L"--autostart") != std::wstring::npos;
    // 旧版注册表项没有 --autostart，读一次顺手改写，下次开机即静默启动
    GetAutoStart();

    // v2.0：必须在任何窗口/DC 创建之前声明 DPI 感知
    EnableDpiAwareness();

    // 单实例：同一时刻只允许一个 Tickly 进程。重复双击 exe 时不再启动
    // 第二个副本，而是直接唤起已运行的窗口（还原/显示并置前），同微信/QQ
    HANDLE hMutex = CreateMutexW(nullptr, TRUE,
                                 L"Local\\Tickly_8F3C_SingleInstance");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"TicklyClass", nullptr);
        if (prev) {
            if (IsIconic(prev))              ShowWindow(prev, SW_RESTORE);
            else if (!IsWindowVisible(prev)) ShowWindow(prev, SW_SHOW);
            SetWindowPos(prev, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(prev);
        }
        CloseHandle(hMutex);
        return 0;                            // 第二实例直接退出
    }

    HDC sdc = GetDC(nullptr);
    g_dpi = (UINT)GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);

    INITCOMMONCONTROLSEX icc { sizeof(icc), ICC_DATE_CLASSES | ICC_HOTKEY_CLASS };
    InitCommonControlsEx(&icc);          // 日期时间选择器（DTP）+ 热键控件（v1.12.0）

    // v2.0：标题/副标题/气泡/提醒字体与对勾画笔集中创建（随 DPI 重建）
    CreateUiResources();
    g_fontGroup   = MakeFont(g_grpFontSize);   // v1.13.0 分组字体（大小可设置）
    g_fontGroupBold = MakeFont(g_grpFontSize, true);   // v0.25 未完成分组标题加粗
    g_fontGroupDone = MakeFont(g_doneGrpFontSize);     // v1.13.6 已完成分组字体
    g_fontDate    = MakeFont(g_dtFontSize);    // v0.26 日期时间字体（大小可设置）
    g_font        = MakeFont(g_taskFontSize);  // v1.13.0 任务字体（大小可设置）
    g_fontDone    = MakeFont(g_taskFontSize, false, true);
    g_brWhite   = CreateSolidBrush(COL_WHITE);
    // v0.27：窗口类背景刷随主题重建（ApplyTheme 里重新创建并 SetClassLongPtr）
    g_brBg      = CreateSolidBrush(COL_BG);

    WNDCLASSEXW wc {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    // v2.0：大/小图标都按系统当前尺寸取（标题栏与任务栏各自命中最合适的
    // 一张），而不是把 32x32 直接塞给小图标位再缩小 —— 那会糊成一团
    wc.hIcon         = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                         GetSystemMetrics(SM_CXICON),
                                         GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm       = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = L"TicklyClass";
    if (!wc.hIcon)   wc.hIcon   = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // v1.13.0：自绘列表窗口类（替代 ListBox，像素级平滑滚动）
    // v0.15：补注册 CS_DBLCLKS —— v1.13.0 漏掉后双击不再产生
    // WM_LBUTTONDBLCLK，任务无法双击进入行内编辑
    WNDCLASSEXW lc {};
    lc.cbSize        = sizeof(lc);
    lc.style         = CS_DBLCLKS;
    lc.lpfnWndProc   = TodoListProc;
    lc.hInstance     = hInst;
    lc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    lc.hbrBackground = nullptr;
    lc.lpszClassName = L"TicklyList";
    RegisterClassExW(&lc);

    // v1.12.0：划词结果气泡窗口类
    WNDCLASSEXW tc {};
    tc.cbSize        = sizeof(tc);
    tc.lpfnWndProc   = ToastProc;
    tc.hInstance     = hInst;
    tc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    tc.hbrBackground = nullptr;
    tc.lpszClassName = L"TicklyToast";
    RegisterClassExW(&tc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, Scale(480), Scale(660) };
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Tickly",
                             style, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                             nullptr, nullptr, hInst, nullptr);

    RECT wa {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_hwnd, nullptr,
                 wa.left + ((wa.right - wa.left) - ww) / 2,
                 wa.top  + ((wa.bottom - wa.top) - wh) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    LoadTasks();
    ApplyTheme();                        // v0.27：按加载的主题模式应用色板/DWM
    ApplyFontSettings();                 // v1.13.0：按加载的字体设置重建字体
    RefreshList();
    ApplyHotkeys();                      // v1.12.0：注册全局快捷键（Alt+Q / Alt+1）

    UINT showCmd = (nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_SHOWMINNOACTIVE)
                       ? SW_SHOWNORMAL : (UINT)nCmdShow;
    if (g_wpSaved) {                     // 恢复上次的窗口大小/位置
        RECT wa {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        const RECT& r = g_wpRect;
        bool ok = (r.right - r.left) >= Scale(220) &&
                  (r.bottom - r.top) >= Scale(320) &&
                  r.right > wa.left + Scale(100) && r.left < wa.right - Scale(100) &&
                  r.bottom > wa.top + Scale(30) && r.top < wa.bottom - Scale(30);
        if (ok)
            SetWindowPos(g_hwnd, nullptr, r.left, r.top,
                         r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
        if (g_wpMax) showCmd = SW_SHOWMAXIMIZED;
    }
    // v2.3.2：开机自启走隐藏分支 —— 窗口不 Show（创建时本就不带 WS_VISIBLE），
    // 托盘图标、提醒定时器、全局热键已在 WM_CREATE 与上面注册好，照常工作
    if (!startHidden) {
        ShowWindow(g_hwnd, (int)showCmd);
        UpdateWindow(g_hwnd);
    }
    CheckReminders();   // 启动时若有到期/逾期任务，立即提醒

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!g_rowEdit && IsDialogMessageW(g_hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return (int)msg.wParam;
}
