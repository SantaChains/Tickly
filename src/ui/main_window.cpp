// 主窗口（布局 / 绘制入口 / 窗口过程）
#include "common.h"
#include "ui/main_window.h"
#include "app/actions.h"
#include "ui/capture.h"
#include "ui/dialogs_date.h"
#include "ui/dialogs_settings.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "ui/hotkeys.h"
#include "app/ids.h"
#include "ui/input_box.h"
#include "ui/list_view.h"
#include "ui/reminder.h"
#include "ui/row_editor.h"
#include "ui/context.h"
#include "app/store.h"
#include "core/task.h"
#include "ui/theme.h"
#include "core/time_util.h"
#include "ui/toast.h"
#include "ui/tray.h"


// ---------------------------- 合并落盘 --------------------------------------
// 连续操作（勾选一批、连续删除）不再每次全量重写 tasks.txt：
// 变更只置脏并重置一个 300ms 的一次性定时器，最后一笔操作后统一写盘。
// 退出/关机/拖动结束走 FlushSave 立即写，异常退出最多丢最近 300ms 的改动。
static bool s_saveDirty = false;

// 写盘前把当前窗口位置同步到 g_wp*，store 只负责序列化，不碰窗口句柄
static void CaptureWindowPlacement() {
    if (!g_hwnd) return;
    WINDOWPLACEMENT wp { sizeof(wp) };
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    g_wpRect  = wp.rcNormalPosition;
    g_wpMax   = wp.showCmd == SW_SHOWMAXIMIZED;
    g_wpSaved = true;
}

void RequestSave() {
    CaptureWindowPlacement();
    s_saveDirty = true;
    if (g_hwnd) SetTimer(g_hwnd, 6, 300, nullptr);   // 同 ID 重调 = 重新计时
    else FlushSave();
}

void FlushSave() {
    if (g_hwnd) KillTimer(g_hwnd, 6);
    if (!s_saveDirty) return;
    s_saveDirty = false;
    CaptureWindowPlacement();
    SaveTasks();
}

// ---------------------------- 布局 ------------------------------------------
void Layout() {
    RECT rc {};
    GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int m       = Scale(14);
    int inputH  = (g_inputH > 0) ? g_inputH : Scale(36);   // v1.12.0 随行数长高
    int topY    = Scale(72);
    int addW    = Scale(96);
    int hintH   = Scale(22);
    int listTop = topY + inputH + hintH + Scale(6);
    // v1.13.12：底栏整体移除，列表直接铺到窗口底部（下留一个 m 的边距）
    int listH   = H - listTop - m;
    if (listH < Scale(80)) listH = Scale(80);

    // v1.13.17：改变输入框尺寸会让 EDIT 重排，插入点可能被清掉。改动前后
    // 保存/恢复一次（只在它持有焦点时），这是所有布局路径的最后一道保险
    DWORD inSel0 = 0, inSel1 = 0;
    bool  inFocus = (g_input && GetFocus() == g_input);
    if (inFocus)
        SendMessageW(g_input, EM_GETSEL, (WPARAM)&inSel0, (LPARAM)&inSel1);
    MoveWindow(g_input,  m, topY, W - m * 2 - addW - Scale(8), inputH, TRUE);
    MoveWindow(g_btnAdd, W - m - addW, topY, addW, inputH, TRUE);
    // v1.13.11：列表内缩 1px，父窗口画圆角卡片描边，列表自身用圆角 region
    // 裁剪 —— 得到真正的圆角白卡，而不是直角白块
    MoveWindow(g_list,   m + 1, listTop + 1, W - m * 2 - 2, listH - 2, TRUE);
    {
        int rad = Scale(12);
        HRGN rgn = CreateRoundRectRgn(0, 0, W - m * 2 - 1, listH - 1,
                                      rad, rad);
        SetWindowRgn(g_list, rgn, TRUE);   // 成功后 region 归系统所有
    }
    CenterInput();
    if (inFocus) {
        int len = (int)SendMessageW(g_input, WM_GETTEXTLENGTH, 0, 0);
        if ((int)inSel0 > len) inSel0 = (DWORD)len;
        if ((int)inSel1 > len) inSel1 = (DWORD)len;
        SendMessageW(g_input, EM_SETSEL, (WPARAM)inSel0, (LPARAM)inSel1);
    }
    // v0.27：输入框外描边矩形（父窗口 WM_PAINT 绘制）。v0.27.1：外扩量
    // 1px → Scale(2) —— 圆角弧线相对外接矩形最大下沉约 0.29×半径
    // （1px 笔宽再 +0.5px），外扩 1px 时弧线探进输入框窗口被其不透明
    // 底色盖掉（四角缺线）；外扩 Scale(2) 后弧线完整落在父窗口区域
    SetRect(&g_inputFrame, m - Scale(2), topY - Scale(2),
            W - m - addW - Scale(8) + Scale(2), topY + inputH + Scale(2));

    // 右上角「更多」：与标题同一视觉高度
    int moreW = Scale(34), moreH = Scale(26);
    MoveWindow(g_btnMore, W - m - moreW, Scale(14), moreW, moreH, TRUE);

    SetRect(&g_listFrame, m - 1, listTop - 1, W - m + 1, listTop + listH + 1);
    SetRect(&g_hintRect, m + Scale(2), topY + inputH + Scale(3),
            W - m, topY + inputH + Scale(3) + Scale(18));
}

// ---------------------------- 主按钮悬停跟踪（v1.13.10） ---------------------
static LRESULT CALLBACK BtnAddProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEMOVE && !g_btnAddHot) {
        g_btnAddHot = true;
        TRACKMOUSEEVENT tme { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        InvalidateRect(h, nullptr, TRUE);
    } else if (m == WM_MOUSELEAVE) {
        g_btnAddHot = false;
        InvalidateRect(h, nullptr, TRUE);
    }
    return CallWindowProcW(g_oldBtnAddProc, h, m, w, l);
}

// v1.13.11：右上角「⋯」按钮的悬停跟踪
static LRESULT CALLBACK BtnMoreProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEMOVE && !g_btnMoreHot) {
        g_btnMoreHot = true;
        TRACKMOUSEEVENT tme { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        InvalidateRect(h, nullptr, TRUE);
    } else if (m == WM_MOUSELEAVE) {
        g_btnMoreHot = false;
        InvalidateRect(h, nullptr, TRUE);
    }
    return CallWindowProcW(g_oldBtnMoreProc, h, m, w, l);
}

// ---------------------------- 窗口过程 --------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lParam)->hInstance;
        // 多行 EDIT + EM_SETRECT 实现光标垂直居中；ES_AUTOVSCROLL：粘贴多行
        // 文本时自动换行（v1.12.0），超过 4 行内部滚动；
        // ES_WANTRETURN（v0.16）：Shift+Enter 由默认过程插入换行
        // v0.27：去掉 WS_EX_CLIENTEDGE 经典下凹边框（父窗口画 1px 圆角描边）
        g_input = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_INPUT, hi, nullptr);
        SendMessageW(g_input, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(Scale(8), Scale(8)));   // 去边框后补内边距
        // v0.27：五个按钮全部改为自绘（BS_OWNERDRAW）——「＋ 添加」为强调色
        // 实心圆角主按钮，其余为幽灵描边按钮，绘制在 WM_DRAWITEM
        g_btnAdd = CreateWindowExW(0, L"BUTTON", L"\uFF0B 添加",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_BTN_ADD, hi, nullptr);
        // v1.13.0：列表改为完全自绘子窗口 TicklyList（像素级平滑滚动、
        // 悬浮滚动条、双缓冲无闪烁；不再使用 ListBox）
        g_list = CreateWindowExW(0, L"TicklyList", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_LIST, hi, nullptr);
        g_btnMore = CreateWindowExW(0, L"BUTTON", L"⋯",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_BTN_MORE, hi, nullptr);

        SendMessageW(g_input,     WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(g_list,      WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(g_btnAdd,    WM_SETFONT, (WPARAM)g_fontSub, TRUE);
        g_oldBtnAddProc = (WNDPROC)SetWindowLongPtrW(g_btnAdd, GWLP_WNDPROC,
                                                     (LONG_PTR)BtnAddProc);
        SendMessageW(g_btnMore,   WM_SETFONT, (WPARAM)g_fontSub, TRUE);
        g_oldBtnMoreProc = (WNDPROC)SetWindowLongPtrW(g_btnMore, GWLP_WNDPROC,
                                                      (LONG_PTR)BtnMoreProc);

        g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_input, GWLP_WNDPROC,
                                                   (LONG_PTR)EditProc);
        SetTimer(hwnd, 1, 15000, nullptr);   // 周期检查任务提醒
        g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        // 托盘常驻三步：① 预写（上次运行遗留的设置子键先置 IsPromoted=1，
        // Explorer 添加图标那一刻会读取）② 添加图标 ③ 兑底（未生效则写入
        // + 重加 + 定时重试，覆盖首启子键异步建立的情况）
        g_trayAddTries = 0;
        g_aliveMiss    = 0;
        g_trayRollback = false;
        int pre = PromoteTrayIcon();         // ① 预写，记住结果用于验证期判定
        AddTrayIcon();                       // ② 任务栏托盘图标
        SetTimer(hwnd, 5, 5000, nullptr);    // ④ 心跳保活：图标丢了 10 秒内自动恢复
        if (!g_nidAdded) EnsureTrayIcon(hwnd);
        else {
            g_promoteTries = 0;
            if (pre != TRAY_PROMOTE_NONE) {
                g_trayPromoted = true;       // 预写生效（或本就常驻）
                BeginTrayAliveCheck();       // 心跳确认常驻无副作用
            }
            ApplyTrayPromotion(hwnd);        // ③ 图标常驻右下角，不被收进「^」折叠区
        }
        Layout();
        ApplyTheme();        // v0.27：窗口创建即应用 DWM 外壳与主题色板
                             //（此时尚未 LoadTasks，按默认「跟随系统」解析；
                             //  加载持久化设置后 wWinMain 会再调一次）
        return 0;
    }

    case WM_DPICHANGED: {
        // v2.0：跨显示器拖动或系统缩放比例变化。用新 DPI 重建字体/画笔，
        // 再按系统建议矩形改窗口大小 —— 否则窗口尺寸与字号仍是旧比例
        UINT nd = HIWORD(wParam);          // Y 轴 DPI（X/Y 通常一致）
        if (nd) g_dpi = nd;
        if (g_list) {                      // 创建期（子窗口未就绪）只调整尺寸
            CreateUiResources();           // 标题/气泡/提醒字体 + 对勾画笔
            ApplyFontSettings();           // 任务/分组/日期字体 + 行高 + 布局
        }
        const RECT* pr = (const RECT*)lParam;
        if (pr)
            SetWindowPos(hwnd, nullptr, pr->left, pr->top,
                         pr->right - pr->left, pr->bottom - pr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_SIZE:
        CloseRowEditor(true);            // 窗口尺寸变化，行位置改变
        Layout();
        // v1.13.0：填塞行/行高/滚动钳制由列表窗口自身 WM_SIZE 处理
        if (wParam != SIZE_MINIMIZED && !g_wpTimer)
            g_wpTimer = SetTimer(hwnd, 2, 1000, nullptr);   // 1 秒后落盘新尺寸
        return 0;

    case WM_EXITSIZEMOVE:                // 拖动/缩放结束 -> 立即保存窗口位置大小
        if (g_wpTimer) { KillTimer(hwnd, 2); g_wpTimer = 0; }
        RequestSave();                   //（关机/重启/强杀时 WM_DESTROY 不会执行，
        FlushSave();                     //  仅靠退出时保存会丢失最后一次调整）
        return 0;

    case WM_ENDSESSION:                  // Windows 关机/重启 -> 尽快落盘一次
        if (wParam) { RequestSave(); FlushSave(); }
        return 0;

    case WM_SETFOCUS:
        SetFocus(g_input);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = Scale(440);
        mmi->ptMinTrackSize.y = Scale(560);
        return 0;
    }

    case WM_CLOSE:                       // 点 X：按设置决定托盘后台或真正退出
        if (g_closeToTray) HideToTray(); //   勾选（默认）：缩到托盘，提醒照常弹出
        else DestroyWindow(hwnd);        //   取消勾选：直接退出
        return 0;

    case WM_TRAY: {                      // 托盘图标交互
        if (lParam == WM_LBUTTONUP) {
            ShowMainWindow();            // 左键：显示/还原主窗口
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            if (menu) {
                AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"显示主窗口");
                AppendMenuW(menu, MF_STRING, IDM_TRAY_SET,  L"设置…");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"退出");
                SetForegroundWindow(hwnd);   // 保证点击菜单外能关闭菜单
                TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, pt.x, pt.y, hwnd, nullptr);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(menu);
            }
        }
        return 0;
    }

    case WM_OPENDATE:                    // 单击日期标签 -> 修改日期时间
        if ((int)wParam >= 0 && (int)wParam < (int)g_tasks.size())
            OpenDateEditor((int)wParam);
        return 0;

    case WM_HOTKEY:                      // v1.12.0 全局快捷键
        if (wParam == HK_ID_SHOW)          ShowMainWindow();
        else if (wParam == HK_ID_CAP)      CaptureSelectedToTask();
        return 0;

    case WM_COMMAND: {
        int id   = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == IDOK)                            { AddFromInput();   return 0; }
        if (id == IDCANCEL)                        { SetWindowTextW(g_input, L""); SetFocus(g_input); return 0; }
        if (id == IDC_BTN_ADD)                     { AddFromInput();   return 0; }
        if (id == IDC_BTN_MORE) {                   // v1.13.11：右上角「⋯」菜单
            HMENU menu = CreatePopupMenu();
            if (menu) {
                int done = CountDone();
                wchar_t lab[64];
                AppendMenuW(menu, MF_STRING, IDM_APP_SETTINGS, L"设置…");
                std::swprintf(lab, 64, L"清除已完成（%d）", done);
                AppendMenuW(menu, MF_STRING | (done > 0 ? MF_ENABLED : MF_GRAYED),
                            IDM_APP_CLEAR, lab);
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"退出 Tickly");
                RECT br {};
                GetWindowRect(g_btnMore, &br);
                SetForegroundWindow(hwnd);
                TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_TOPALIGN,
                                 br.left, br.bottom, hwnd, nullptr);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(menu);
            }
            return 0;
        }
        if (id == IDM_APP_CLEAR) {                  // 二次确认，防止误删
            int done = CountDone();
            wchar_t msg[128];
            std::swprintf(msg, 128,
                L"确定要清除全部 %d 项已完成的任务吗？\n清除后无法恢复。", done);
            if (done > 0 &&
                MessageBoxW(hwnd, msg, L"清除已完成",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
                ClearDone();
            return 0;
        }
        if (id == IDM_APP_SETTINGS)                { OpenSettings();   return 0; }
        if (id == IDM_TRAY_SHOW)                   { ShowMainWindow(); return 0; }
        if (id == IDM_TRAY_SET)                    { ShowMainWindow(); OpenSettings(); return 0; }
        if (id == IDM_TRAY_EXIT)                   { DestroyWindow(hwnd); return 0; }
        if (id == IDM_ROW_DELETE)                  { DeleteTaskAt(g_menuRow); g_menuRow = -1; return 0; }
        if (id == IDM_MULTI_DONE)                  { MultiSetDone(g_multiSel, true);  return 0; }   // v0.16
        if (id == IDM_MULTI_UNDONE)                { MultiSetDone(g_multiSel, false); return 0; }   // v0.16
        if (id == IDM_MULTI_DELETE)                { MultiDelete(g_multiSel);         return 0; }   // v0.16
        if (id == IDM_ROW_EDITDONE) {              // v0.15：修改完成日期
            if (g_menuRow >= 0 && g_menuRow < (int)g_rows.size() &&
                g_rows[g_menuRow] >= 0)
                OpenDoneDateEditor(g_rows[g_menuRow]);
            g_menuRow = -1;
            return 0;
        }
        if (id == IDC_INPUT && code == EN_CHANGE)  {
            // v1.13.16：EN_CHANGE 是 EDIT 在「处理输入的过程中」同步发给父窗口的。
            // 此刻若对同一个控件发 EM_SETRECT / EM_SETSEL，等于在它插入文字的
            // 中途改写它的选区 —— 中文输入法第二段上屏时（文本变长、行数或格式
            // 矩形随之变化）会触发选区恢复，把插入点顶回组合串起点，表现为
            // 「『下午』一上屏光标就跑到前面」。改为投递消息，等 EDIT 把这次输入
            // 彻底处理完、消息循环空闲时再做布局与提示更新
            PostMessageW(hwnd, WM_INPUTCHANGED, 0, 0);
            return 0;
        }
        break;
    }

    case WM_INPUTCHANGED: {              // v1.13.16：输入框内容变化的延迟处理
        UpdateInputHint();
        UpdateInputSize();
        InvalidateRect(g_input, nullptr, TRUE);   // 占位符随空/非空切换
        return 0;
    }

    case WM_DRAWITEM: {                  // v0.27：底部按钮自绘
        const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
        if (!dis || dis->CtlType != ODT_BUTTON) break;
        bool sel = (dis->itemState & ODS_SELECTED) != 0;
        bool dis_ = (dis->itemState & ODS_DISABLED) != 0;
        bool primary = (dis->CtlID == IDC_BTN_ADD);   // 主操作 = 最高频的「＋ 添加」
        // v2.1：先铺父窗口底色。自绘按钮的底色由系统按 BTNFACE 预先擦除，
        // 深色模式下这块浅灰就露了出来 ——「⋯」按钮整块发白、主按钮圆角外
        // 四个角出现白线。铺一层 COL_BG 后，圆角外与未绘制区域与页面底一致
        FillSolid(dis->hDC, dis->rcItem, COL_BG);
        if (dis->CtlID == IDC_BTN_MORE) {             // v1.13.11：右上角「⋯」
            RECT r = dis->rcItem;
            if (g_btnMoreHot) {                       // 悬停：浅灰圆角底
                HGDIOBJ op = SelectObject(dis->hDC, GetStockObject(NULL_PEN));
                HGDIOBJ ob = SelectObject(dis->hDC, CachedBrush(COL_HOVER_BG));
                RoundRect(dis->hDC, r.left, r.top, r.right, r.bottom,
                          Scale(6), Scale(6));
                SelectObject(dis->hDC, ob);
                SelectObject(dis->hDC, op);
            }
            int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            int rr = Scale(2);
            if (rr < 1) rr = 1;
            HGDIOBJ op = SelectObject(dis->hDC, GetStockObject(NULL_PEN));
            HGDIOBJ ob = SelectObject(dis->hDC,
                                      CachedBrush(g_btnMoreHot ? COL_TEXT : COL_MUTED));
            for (int i = -1; i <= 1; ++i)
                Ellipse(dis->hDC, cx + i * Scale(7) - rr, cy - rr,
                        cx + i * Scale(7) + rr, cy + rr);
            SelectObject(dis->hDC, ob);
            SelectObject(dis->hDC, op);
            return TRUE;
        }
        COLORREF bg, tx, bd;
        if (primary) {
            // v1.13.10：主按钮不用整块纯强调色 —— 大面积高饱和色在浅灰底上
            // 很扎眼；浅色下按强调色压暗 12% 作底，深色下直接用强调色。
            // v2.3.2：底色由 COL_ACCENT 派生，换主题色时按钮自动跟随
            COLORREF base = g_themeDark ? COL_ACCENT : DarkenColor(COL_ACCENT, 12);
            if (g_btnAddHot && !sel) base = DarkenColor(base, 6);   // 悬停略沉
            bg = sel ? DarkenColor(base, 12) : base;
            tx = COL_WHITE;
            bd = bg;
        } else {                             // 幽灵按钮：背景色底 + 1px 描边 + 正文色
            bg = sel ? DarkenColor(COL_BG, 10) : COL_BG;
            tx = dis_ ? COL_MUTED : EffTaskCol();
            bd = (dis_ || sel) ? COL_BOX_BD : COL_BORDER;
        }
        if (dis_ && !primary) bg = COL_BG;   // 禁用态不加深
        HGDIOBJ op = SelectObject(dis->hDC, CachedPen(bd));
        HGDIOBJ ob = SelectObject(dis->hDC, CachedBrush(bg));
        RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                  dis->rcItem.right, dis->rcItem.bottom, Scale(6), Scale(6));
        SelectObject(dis->hDC, ob);
        SelectObject(dis->hDC, op);
        if (primary && (dis->itemState & ODS_FOCUS)) {   // 键盘焦点：内圈描边可见
            HGDIOBJ op2 = SelectObject(dis->hDC, CachedPen(DarkenColor(bg, 30)));
            HGDIOBJ ob2 = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            RoundRect(dis->hDC, dis->rcItem.left + 1, dis->rcItem.top + 1,
                      dis->rcItem.right - 1, dis->rcItem.bottom - 1,
                      Scale(5), Scale(5));
            SelectObject(dis->hDC, ob2);
            SelectObject(dis->hDC, op2);
        }
        wchar_t txt[64] {};
        GetWindowTextW(dis->hwndItem, txt, 64);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, tx);
        HFONT bf = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
        HGDIOBJ of = SelectObject(dis->hDC, bf ? (HGDIOBJ)bf
                                               : GetStockObject(DEFAULT_GUI_FONT));
        RECT tr2 = dis->rcItem;
        DrawTextW(dis->hDC, txt, -1, &tr2,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dis->hDC, of);
        return TRUE;
    }

    case WM_CTLCOLORBTN:                 // v2.1：自绘按钮的系统预擦除底色
        // 不返回主题色时，系统用 COLOR_BTNFACE（浅灰）擦按钮，
        // 深色模式下就会在圆角外和「⋯」按钮底上露出白色块/白线
        return (LRESULT)CachedBrush(COL_BG);

    case WM_CTLCOLOREDIT: {              // v0.27：主输入框底色/文字色随主题
        HDC hdc = (HDC)wParam;           //（行内编辑框由列表窗口 WM_CTLCOLOREDIT 处理）
        if (g_input && (HWND)lParam == g_input) {
            SetBkColor(hdc, COL_WHITE);
            SetTextColor(hdc, EffTaskCol());
            return (LRESULT)CachedBrush(COL_WHITE);
        }
        break;
    }

    case WM_SETTINGCHANGE:               // v0.27：跟随系统模式下系统深浅色切换
        if (g_themeMode == 2 && lParam &&
            lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet") == 0)
            ApplyTheme();
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            CheckReminders();
        } else if (wParam == 2) {        // 延时落盘窗口尺寸（贴靠/最大化等）
            KillTimer(hwnd, 2);
            g_wpTimer = 0;
            RequestSave();
        } else if (wParam == 6) {        // 合并落盘到期：把累积的变更写一次盘
            FlushSave();
        } else if (wParam == 3) {        // 托盘图标提升重试（子键异步建立）
            KillTimer(hwnd, 3);
            ApplyTrayPromotion(hwnd);    // 成功或重试耗尽后自动停表
        } else if (wParam == 4) {        // 托盘图标添加失败重试
            KillTimer(hwnd, 4);
            EnsureTrayIcon(hwnd);
            if (g_nidAdded) {
                g_promoteTries = 0;
                if (g_trayPromoted && !g_trayRollback)
                    BeginTrayAliveCheck();   // 常驻图标刚重加：验证期确认
                ApplyTrayPromotion(hwnd);
            }
        } else if (wParam == 5) {        // 托盘心跳保活（v1.9.1 自适应频率）：
                                         // 图标可以被系统折叠，但绝不能消失
            // 节奏自适应：验证期/异常恢复期 5 秒一拍快速收敛；连续 6 拍
            // 一切正常后降到 30 秒低频兜底（每分钟仅 2 次微秒级 IPC 探测，
            // 资源占用几乎为零）。任何异常立即回到 5 秒快拍。
            bool fast = !g_nidAdded || g_trayAliveCheck || g_aliveMiss > 0;
            if (fast) {
                g_hbSteady = 0;
                if (g_hbInterval != 5000) {
                    g_hbInterval = 5000;
                    SetTimer(hwnd, 5, 5000, nullptr);   // 同 ID 重调 = 改间隔
                }
            } else if (++g_hbSteady >= 6 && g_hbInterval != 30000) {
                g_hbInterval = 30000;
                SetTimer(hwnd, 5, 30000, nullptr);
            }
            if (!g_nidAdded) {           // 图标不在：直接补加，下轮继续确认
                AddTrayIcon();
                return 0;
            }
            if (!TrayIconAlive()) {      // NIM_MODIFY 失败 = 图标已被系统移除
                if (++g_aliveMiss < 2) return 0;  // 单次失败可能是瞬时抖动
                g_aliveMiss = 0;
                if (g_trayAliveCheck) {  // 提升验证期内丢失：该系统写
                    RollbackTrayPromotion();      // IsPromoted 反而丢图标，
                    g_trayRollback   = true;      // 回滚为系统默认（折叠但
                    g_trayAliveCheck = false;     // 一定显示）
                    g_aliveOk        = 0;
                } else {                 // 普通丢失：直接恢复
                    RemoveTrayIcon();
                    AddTrayIcon();
                }
                if (!g_nidAdded) EnsureTrayIcon(hwnd);
                return 0;
            }
            g_aliveMiss = 0;
            if (g_trayAliveCheck && ++g_aliveOk >= 2) {  // 连续 2 拍存活：
                g_trayAliveCheck = false;                // 提升确认无害
                g_aliveOk        = 0;
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;   // v0.21：背景由 WM_PAINT 统一填充（COL_BG 全覆盖），
                    // 省一遍整窗擦底 —— 点击/展开时不再整个界面闪一下
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc {};
        GetClientRect(hwnd, &rc);
        FillSolid(hdc, rc, COL_BG);

        int m = g_listFrame.left + 1;

        RECT accent = { m, Scale(18), m + Scale(5), Scale(18) + Scale(26) };
        FillSolid(hdc, accent, COL_ACCENT);

        // v0.27：输入框外 1px 圆角描边 —— 聚焦=强调色，否则=边框色
        //（输入框已去 CLIENTEDGE；EN_SETFOCUS/EN_KILLFOCUS 时重绘）
        // v0.27.1：圆角半径 6→4（外接矩形下沉量随半径减小，配合外扩
        // Scale(2) 保证四角弧线不被输入框底色盖掉；4px 亦为 WinUI 标准）
        if (g_input) {
            COLORREF ibc = (GetFocus() == g_input) ? COL_ACCENT : COL_BORDER;
            HGDIOBJ op = SelectObject(hdc, CachedPen(ibc));
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, g_inputFrame.left, g_inputFrame.top,
                      g_inputFrame.right, g_inputFrame.bottom, Scale(4), Scale(4));
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
        }

        SetBkMode(hdc, TRANSPARENT);
        HFONT oldF = (HFONT)SelectObject(hdc, g_fontTitle);
        SetTextColor(hdc, COL_TEXT);
        // v1.13.11：标题右侧留出「⋯」按钮的位置
        RECT rt = { m + Scale(15), Scale(16), rc.right - m - Scale(44),
                    Scale(16) + Scale(30) };
        DrawTextW(hdc, L"Tickly", -1, &rt,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        // v1.13.11：副标题从「五条操作说明」改为状态摘要 —— 操作提示本来就
        // 没人读，占着一整行；这里改为一眼能看懂的待办概况
        SelectObject(hdc, g_fontSub);
        SetTextColor(hdc, COL_MUTED);
        RECT st = { m + Scale(15), Scale(46), rc.right - m - Scale(44),
                    Scale(46) + Scale(18) };
        int openCnt = 0, todayCnt = 0;
        long long tm0 = TodayMidnight();
        for (const Task& t : g_tasks) {
            if (t.done) continue;
            ++openCnt;
            if (t.due && t.due < tm0 + 86400) ++todayCnt;
        }
        std::wstring sub;
        if (openCnt == 0)
            sub = L"还没有待办 · 在上方输入后回车添加";
        else if (todayCnt > 0)
            sub = std::to_wstring(openCnt) + L" 项待办 · 今天 " +
                  std::to_wstring(todayCnt) + L" 项";
        else
            sub = std::to_wstring(openCnt) + L" 项待办";
        DrawTextW(hdc, sub.c_str(), -1, &st,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        // 日期时间实时识别提示（v0.21：UpdateInputHint 已生成完整文案，
        // 这里原样绘制 —— 原来再包一层「识别到：…（回车添加…）」重复渲染）
        if (!g_hintText.empty()) {
            SelectObject(hdc, g_fontSub);
            SetTextColor(hdc, COL_HIGHLIGHT);
            DrawTextW(hdc, g_hintText.c_str(), -1, &g_hintRect,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }

        // v1.13.11：列表外框改为圆角描边（与列表 region 同曲率）
        {
            HGDIOBJ op = SelectObject(hdc, CachedPen(COL_BORDER));
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, g_listFrame.left, g_listFrame.top,
                      g_listFrame.right, g_listFrame.bottom,
                      Scale(24), Scale(24));
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
        }

        SelectObject(hdc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        RemoveTrayIcon();
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 3);
        KillTimer(hwnd, 4);
        KillTimer(hwnd, 5);   // 托盘心跳保活
        if (g_wpTimer) { KillTimer(hwnd, 2); g_wpTimer = 0; }
        UnregisterHotKey(hwnd, HK_ID_SHOW);   // v1.12.0 全局快捷键
        UnregisterHotKey(hwnd, HK_ID_CAP);
        if (g_hwndToast) DestroyWindow(g_hwndToast);
        FlushSave();
        if (g_fontTitle)   DeleteObject(g_fontTitle);
        if (g_fontSub)     DeleteObject(g_fontSub);
        if (g_fontGroup)   DeleteObject(g_fontGroup);
        if (g_fontGroupBold) DeleteObject(g_fontGroupBold);
        if (g_fontGroupDone) DeleteObject(g_fontGroupDone);
        if (g_fontDate)    DeleteObject(g_fontDate);
        if (g_font)        DeleteObject(g_font);
        if (g_fontDone)    DeleteObject(g_fontDone);
        if (g_fontRemindT) DeleteObject(g_fontRemindT);
        if (g_fontRemindB) DeleteObject(g_fontRemindB);
        if (g_fontToastT)  DeleteObject(g_fontToastT);
        if (g_fontToastB)  DeleteObject(g_fontToastB);
        if (g_brWhite)   DeleteObject(g_brWhite);
        if (g_brBg)      DeleteObject(g_brBg);    // v0.27 窗口类背景刷
        if (g_penCheck)  DeleteObject(g_penCheck);
        PostQuitMessage(0);
        return 0;

    default:
        // Explorer 重启后托盘图标会被移除，重新添加并重新提升
        if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
            RemoveTrayIcon();
            g_trayAddTries = 0;
            AddTrayIcon();
            if (!g_nidAdded) EnsureTrayIcon(hwnd);
            else {
                g_promoteTries = 0;
                if (g_trayPromoted && !g_trayRollback)
                    BeginTrayAliveCheck();   // 新 Explorer 会重读 IsPromoted
                ApplyTrayPromotion(hwnd);
            }
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
