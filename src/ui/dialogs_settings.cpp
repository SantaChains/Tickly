// 设置对话框
#include "common.h"
#include "ui/dialogs_settings.h"
#include "ui/autostart.h"
#include "ui/dialogs_common.h"
#include "ui/dark_mode.h"
#include "ui/gdi_util.h"
#include "ui/hotkeys.h"
#include "app/ids.h"
#include "ui/reminder.h"
#include "ui/context.h"
#include "app/store.h"
#include "ui/theme.h"
#include "ui/tray.h"
#include "app/version.h"


// ---------------------------- 设置对话框 ------------------------------------
static const int IDC_CK_REMIND  = 2101;  // 到点提醒
static const int IDC_ED_MIN     = 2102;  // 提前分钟数
static const int IDC_CK_AUTORUN = 2103;  // 开机自启
static const int IDC_CK_TRAYRUN = 2104;  // 关闭主窗口后托盘后台运行
static const int IDC_ED_TASKFONT = 2107; // v1.13.0 任务字体大小
static const int IDC_SW_TASK     = 2108; // v1.13.0 任务字体颜色色块（点击改色）
static const int IDC_ED_GRPFONT  = 2109; // v1.13.0 未完成分组字体大小
static const int IDC_SW_GROUP    = 2110; // v1.13.0 未完成分组字体颜色色块
static const int IDC_ED_GRPFONT_DONE = 2116; // v1.13.6 已完成分组字体大小
static const int IDC_SW_GROUP_DONE   = 2117; // v1.13.6 已完成分组字体颜色色块
static const int IDC_ED_DTFONT   = 2111; // v0.26 日期时间字体大小
static const int IDC_SW_DT       = 2112; // v0.26 日期时间字体颜色色块（点击改色）
static const int IDC_RB_THEME_L  = 2113; // v0.27 外观：浅色
static const int IDC_RB_THEME_D  = 2114; // v0.27 外观：深色
static const int IDC_RB_THEME_S  = 2115; // v0.27 外观：跟随系统（默认）

// ---------------------------- 设置对话框：自绘复选/单选（v2.3） --------------
// 为什么不用系统控件：带视觉样式（comctl32 v6）后，复选框/单选钮的**文字颜色**
// 由主题决定（恒为黑色），WM_CTLCOLORBTN 里的 SetTextColor 被忽略 ——
// 深色对话框上就变成「黑字看不见」。自绘后文字统一用 COL_TEXT，图形尺寸按
// 当前字体高度取，两套主题下都清晰；顺带和主界面的圆角勾选框风格一致。
static void RepaintChoiceCtrls(HWND h) {
    const int ids[] = { IDC_CK_REMIND, IDC_CK_AUTORUN, IDC_CK_TRAYRUN,
                        IDC_RB_THEME_L, IDC_RB_THEME_D, IDC_RB_THEME_S };
    for (int id : ids)
        if (HWND c = GetDlgItem(h, id)) InvalidateRect(c, nullptr, TRUE);
}

static void DrawDlgChoice(HDC dc, const RECT& rc, const wchar_t* text,
                          bool checked, bool radio, bool focus) {
    FillSolid(dc, rc, COL_BG);                 // 先铺对话框底色（深色模式必需）
    HFONT f = (HFONT)GetCurrentObject(dc, OBJ_FONT);
    HGDIOBJ of = SelectObject(dc, f ? (HGDIOBJ)f : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW tm {};
    GetTextMetricsW(dc, &tm);

    int box = tm.tmHeight - 2;                 // 方框与文字同高（比 13px 经典位图大）
    if (box < 12) box = 12;
    int h = rc.bottom - rc.top;
    if (box > h) box = h;
    int top  = rc.top + (h - box) / 2;
    int left = rc.left + 1;
    RECT br { left, top, left + box, top + box };

    if (radio) {                               // 单选：圆环 + 实心点
        HGDIOBJ op = SelectObject(dc, CachedPen(checked ? COL_ACCENT : COL_BOX_BD));
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, br.left, br.top, br.right, br.bottom);
        if (checked) {
            SelectObject(dc, CachedBrush(COL_ACCENT));
            int in = box / 4;
            Ellipse(dc, br.left + in, br.top + in, br.right - in, br.bottom - in);
        }
        SelectObject(dc, ob);
        SelectObject(dc, op);
    } else {                                   // 复选：圆角方块 + 白勾
        int rad = box / 4;
        if (rad < 3) rad = 3;
        HGDIOBJ op = SelectObject(dc, CachedPen(checked ? COL_ACCENT : COL_BOX_BD));
        HGDIOBJ ob = SelectObject(dc, CachedBrush(checked ? COL_ACCENT : COL_BG));
        RoundRect(dc, br.left, br.top, br.right, br.bottom, rad, rad);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        if (checked && g_penCheck) {
            HGDIOBJ oc = SelectObject(dc, g_penCheck);
            MoveToEx(dc, br.left + MulDiv(box, 24, 100),
                         br.top  + MulDiv(box, 53, 100), nullptr);
            POINT pts[2] = {
                { br.left + MulDiv(box, 42, 100), br.top + MulDiv(box, 71, 100) },
                { br.left + MulDiv(box, 77, 100), br.top + MulDiv(box, 31, 100) },
            };
            PolylineTo(dc, pts, 2);
            SelectObject(dc, oc);
        }
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_TEXT);
    RECT tr { br.right + box / 2, rc.top, rc.right, rc.bottom };
    DrawTextW(dc, text, -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (focus) {                               // 键盘焦点：文字下划虚线框
        HGDIOBJ op = SelectObject(dc, CachedPen(COL_MUTED));
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RECT fr { br.right + box / 4, rc.top + 1, rc.right - 1, rc.bottom - 1 };
        RoundRect(dc, fr.left, fr.top, fr.right, fr.bottom, 4, 4);
        SelectObject(dc, ob);
        SelectObject(dc, op);
    }
    SelectObject(dc, of);
}

// v2.3.1：BS_OWNERDRAW 按钮**不保存勾选状态** —— BM_SETCHECK 对它无效
// （实测 CheckDlgButton(h,id,BST_CHECKED) 之后 BM_GETCHECK 仍返回 0），
// 所以状态自己存在控件的 GWLP_USERDATA 里，初始化 / 绘制 / 点击 / 保存
// 统一走这三个函数，避免「点了不打勾」。
static bool DlgChkGet(HWND h, int id) {
    HWND c = GetDlgItem(h, id);
    return c && (GetWindowLongPtrW(c, GWLP_USERDATA) & 1) != 0;
}
static void DlgChkSet(HWND h, int id, bool on) {
    HWND c = GetDlgItem(h, id);
    if (!c) return;
    SetWindowLongPtrW(c, GWLP_USERDATA, on ? 1 : 0);
    InvalidateRect(c, nullptr, TRUE);
}
static void DlgThemeSet(HWND h, int mode) {      // 三个主题单选互斥
    DlgChkSet(h, IDC_RB_THEME_L, mode == 0);
    DlgChkSet(h, IDC_RB_THEME_D, mode == 1);
    DlgChkSet(h, IDC_RB_THEME_S, mode == 2);
}


// v4：快捷键采集框自绘。comctl32 的 msctls_hotkey32 用系统色画（深色模式下是
// 白底黑字），这里只接管 WM_PAINT / WM_ERASEBKGND，键盘采集逻辑仍交给原控件，
// 行为不变。
static LRESULT CALLBACK HotkeyBoxProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;                       // WM_PAINT 里整块填充
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, HotkeyBoxProc, 1);
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps {};
        HDC dc = BeginPaint(h, &ps);
        RECT rc {};
        GetClientRect(h, &rc);
        FillSolid(dc, rc, COL_WHITE);
        const WORD hk = (WORD)SendMessageW(h, HKM_GETHOTKEY, 0, 0);
        std::wstring txt = HotkeyKeyName(LOBYTE(hk));
        if (txt.empty()) txt = L"未设置";
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, EffTaskCol());
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        HGDIOBJ of = SelectObject(dc, f ? (HGDIOBJ)f : GetStockObject(DEFAULT_GUI_FONT));
        RECT tr = rc;
        tr.left  += Scale(6);
        tr.right -= Scale(4);
        DrawTextW(dc, txt.c_str(), -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, of);
        HGDIOBJ op = SelectObject(dc, CachedPen(GetFocus() == h ? COL_ACCENT : COL_BOX_BD));
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, nullptr, FALSE);
        break;
    default:
        break;
    }
    const LRESULT r = DefSubclassProc(h, m, w, l);
    if (m == WM_KEYUP || m == WM_SYSKEYUP || m == WM_CHAR)
        InvalidateRect(h, nullptr, FALSE);   // 采集结果变了，重画
    return r;
}
static INT_PTR CALLBACK SettingsDlgProc(HWND h, UINT m, WPARAM w, LPARAM lParam) {
    // v1.13.0：字体设置临时态（对话框生存期）+ 色块画刷 + 自定义色盘记忆
    static COLORREF sTaskCol = 0, sGrpCol = 0, sDtCol = 0, sDoneGrpCol = 0;
    static HBRUSH   sBrTask = nullptr, sBrGrp = nullptr, sBrDt = nullptr,
                    sBrGrpDone = nullptr;
    static COLORREF sCust[16] = {};
    switch (m) {
    case WM_INITDIALOG:
        sTaskCol = g_taskFontCol;
        sGrpCol  = g_grpFontCol;
        sDtCol   = g_dtFontCol;
        sDoneGrpCol = g_doneGrpFontCol;
        sBrTask  = CreateSolidBrush(sTaskCol);
        sBrGrp   = CreateSolidBrush(sGrpCol);
        sBrDt    = CreateSolidBrush(sDtCol);
        sBrGrpDone = CreateSolidBrush(sDoneGrpCol);
        DlgChkSet(h, IDC_CK_REMIND, g_remindOn);
        SetDlgItemInt(h, IDC_ED_MIN, (UINT)(g_remindMin < 0 ? 0 : g_remindMin), FALSE);
        SetDlgItemInt(h, IDC_ED_TASKFONT, (UINT)g_taskFontSize, FALSE);
        SetDlgItemInt(h, IDC_ED_GRPFONT, (UINT)g_grpFontSize, FALSE);
        SetDlgItemInt(h, IDC_ED_GRPFONT_DONE, (UINT)g_doneGrpFontSize, FALSE);
        SetDlgItemInt(h, IDC_ED_DTFONT, (UINT)g_dtFontSize, FALSE);
        DlgChkSet(h, IDC_CK_AUTORUN, GetAutoStart());
        DlgChkSet(h, IDC_CK_TRAYRUN, g_closeToTray);
        // v0.27：外观（主题三态）单选钮初始态
        DlgThemeSet(h, g_themeMode);
        {
            // v1.12.0 采集框；v0.15 起固定 Alt 修饰：控件前显示不可修改的
            // 「Alt+」文本，采集框内按 Ctrl/Shift/Win 组合一律剥掉修饰键
            // 只保留主键（HKCOMB_* 全部禁用，替换为无修饰键）
            DWORD bad = HKCOMB_A | HKCOMB_C | HKCOMB_S |
                        HKCOMB_CA | HKCOMB_SA | HKCOMB_SC | HKCOMB_SCA;
            HWND hk1 = GetDlgItem(h, IDC_HK_SHOW);
            HWND hk2 = GetDlgItem(h, IDC_HK_CAPTURE);
            SendMessageW(hk1, HKM_SETRULES, bad, MAKELPARAM(0, 0));
            SendMessageW(hk2, HKM_SETRULES, bad, MAKELPARAM(0, 0));
            SendMessageW(hk1, HKM_SETHOTKEY, MAKEWPARAM((WORD)g_hkShowVk, 0), 0);
            SendMessageW(hk2, HKM_SETHOTKEY, MAKEWPARAM((WORD)g_hkCapVk, 0), 0);
            // 采集框自绘（深色模式下不再是白底）
            SetWindowSubclass(hk1, HotkeyBoxProc, 1, 0);
            SetWindowSubclass(hk2, HotkeyBoxProc, 1, 0);
            InvalidateRect(hk1, nullptr, TRUE);
            InvalidateRect(hk2, nullptr, TRUE);
        }
        RepaintChoiceCtrls(h);   // v2.3：状态已写入，让自绘控件按最新勾选重画
        // 深色模式下把系统控件（编辑框 / DTP / 快捷键采集框）和标题栏一起切深
        DarkModeApplyToWindow(h);
        return TRUE;
    case WM_CTLCOLORSTATIC: {
        // v1.13.0：字体颜色色块用当前颜色画刷填充
        HWND ct = (HWND)lParam;
        int  id = (ct && IsWindow(ct)) ? GetDlgCtrlID(ct) : 0;
        if (id == IDC_SW_TASK && sBrTask) return (INT_PTR)sBrTask;
        if (id == IDC_SW_GROUP && sBrGrp) return (INT_PTR)sBrGrp;
        if (id == IDC_SW_GROUP_DONE && sBrGrpDone) return (INT_PTR)sBrGrpDone;
        if (id == IDC_SW_DT && sBrDt) return (INT_PTR)sBrDt;
        // v0.27：其余静态文本（含单选/复选标签）随主题配色；编辑框、
        // DTP、快捷键采集框等系统控件本期限随系统样式
        SetTextColor((HDC)w, COL_TEXT);
        SetBkColor((HDC)w, COL_BG);
        return (INT_PTR)CachedBrush(COL_BG);
    }
    case WM_CTLCOLOREDIT:                // 编辑框：底色/文字随主题（深色下不再是白底）
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)w, COL_TEXT);
        SetBkColor((HDC)w, COL_WHITE);
        return (INT_PTR)CachedBrush(COL_WHITE);
    case WM_CTLCOLORDLG:                 // v0.27：对话框背景随主题
        SetTextColor((HDC)w, COL_TEXT);
        SetBkColor((HDC)w, COL_BG);
        return (INT_PTR)CachedBrush(COL_BG);
    case WM_CTLCOLORBTN:                 // v0.27：按钮背景刷随主题（复选/单选周围底色）
        return (INT_PTR)CachedBrush(COL_BG);
    case WM_DRAWITEM: {                  // v2.3：自绘复选/单选（文字色跟主题）
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lParam;
        if (!di || di->CtlType != ODT_BUTTON) break;
        int id = (int)di->CtlID;
        bool radio = (id == IDC_RB_THEME_L || id == IDC_RB_THEME_D ||
                      id == IDC_RB_THEME_S);
        if (!radio && id != IDC_CK_REMIND && id != IDC_CK_AUTORUN &&
            id != IDC_CK_TRAYRUN)
            break;
        wchar_t txt[192] {};
        GetWindowTextW(di->hwndItem, txt, 192);
        DrawDlgChoice(di->hDC, di->rcItem, txt, DlgChkGet(h, id),
                      radio, (di->itemState & ODS_FOCUS) != 0);
        return TRUE;
    }
    case WM_COMMAND:
        // v2.3：自绘的复选按钮不会自己切换状态，这里手动维护
        if (HIWORD(w) == BN_CLICKED &&
            (LOWORD(w) == IDC_CK_REMIND || LOWORD(w) == IDC_CK_AUTORUN ||
             LOWORD(w) == IDC_CK_TRAYRUN)) {
            int id = LOWORD(w);
            DlgChkSet(h, id, !DlgChkGet(h, id));   // 自绘按钮不自动切换
            return TRUE;
        }
        if (LOWORD(w) == IDC_SW_TASK || LOWORD(w) == IDC_SW_GROUP ||
            LOWORD(w) == IDC_SW_DT || LOWORD(w) == IDC_SW_GROUP_DONE) {
            if (HIWORD(w) != STN_CLICKED) break;
            // 0任务 / 1未完成组 / 2日期 / 3已完成组（v1.13.6）
            int which = (LOWORD(w) == IDC_SW_TASK) ? 0 :
                        (LOWORD(w) == IDC_SW_GROUP) ? 1 :
                        (LOWORD(w) == IDC_SW_DT) ? 2 : 3;
            CHOOSECOLORW cc { sizeof(cc) };
            cc.hwndOwner   = h;
            cc.lpCustColors = sCust;
            cc.rgbResult    = which == 0 ? sTaskCol : which == 1 ? sGrpCol :
                              which == 2 ? sDtCol   : sDoneGrpCol;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                COLORREF c = cc.rgbResult;
                if (which == 0) {
                    sTaskCol = c;
                    if (sBrTask) DeleteObject(sBrTask);
                    sBrTask = CreateSolidBrush(c);
                } else if (which == 1) {
                    sGrpCol = c;
                    if (sBrGrp) DeleteObject(sBrGrp);
                    sBrGrp = CreateSolidBrush(c);
                } else if (which == 2) {
                    sDtCol = c;
                    if (sBrDt) DeleteObject(sBrDt);
                    sBrDt = CreateSolidBrush(c);
                } else {
                    sDoneGrpCol = c;
                    if (sBrGrpDone) DeleteObject(sBrGrpDone);
                    sBrGrpDone = CreateSolidBrush(c);
                }
                InvalidateRect(GetDlgItem(h, LOWORD(w)), nullptr, TRUE);
            }
            return TRUE;
        }
        if (LOWORD(w) == IDC_RB_THEME_L || LOWORD(w) == IDC_RB_THEME_D ||
            LOWORD(w) == IDC_RB_THEME_S) {
            // v0.27：切外观立即生效并联存（F 行）——即使随后取消，
            // 外观选择也保留（与字体即时预览同权重）
            if (HIWORD(w) != BN_CLICKED) break;
            // v2.3.1：自绘单选钮的互斥状态同样自己维护
            g_themeMode = (LOWORD(w) == IDC_RB_THEME_L) ? 0 :
                          (LOWORD(w) == IDC_RB_THEME_D) ? 1 : 2;
            DlgThemeSet(h, g_themeMode);
            ApplyTheme();                    // 界面立即生效
            // v2.3：主题色变了，整框重绘 —— 静态文本与自绘复选/单选都要换色
            InvalidateRect(h, nullptr, TRUE);
            SaveTasks();                     // 触发一次设置保存（F 行）
            return TRUE;
        }
        if (LOWORD(w) == IDOK) {
            g_remindOn = DlgChkGet(h, IDC_CK_REMIND);
            UINT mv = GetDlgItemInt(h, IDC_ED_MIN, nullptr, FALSE);
            g_remindMin = (mv > 10080) ? 10080 : (int)mv;
            SetAutoStart(DlgChkGet(h, IDC_CK_AUTORUN));
            g_closeToTray = DlgChkGet(h, IDC_CK_TRAYRUN);
            // v1.13.0：字体大小（任务 12~22 / 分组 10~18 / 日期 10~18）与颜色
            UINT tf = GetDlgItemInt(h, IDC_ED_TASKFONT, nullptr, FALSE);
            UINT gf = GetDlgItemInt(h, IDC_ED_GRPFONT, nullptr, FALSE);
            UINT gfd = GetDlgItemInt(h, IDC_ED_GRPFONT_DONE, nullptr, FALSE);
            UINT df = GetDlgItemInt(h, IDC_ED_DTFONT, nullptr, FALSE);
            g_taskFontSize = (tf < 12) ? 12 : (tf > 22 ? 22 : (int)tf);
            g_grpFontSize  = (gf < 10) ? 10 : (gf > 18 ? 18 : (int)gf);
            g_doneGrpFontSize = (gfd < 10) ? 10 : (gfd > 18 ? 18 : (int)gfd);
            g_dtFontSize   = (df < 10) ? 10 : (df > 18 ? 18 : (int)df);
            g_taskFontCol  = sTaskCol;
            g_grpFontCol   = sGrpCol;
            g_doneGrpFontCol = sDoneGrpCol;
            g_dtFontCol    = sDtCol;
            ApplyFontSettings();             // 字体/行高/折行全部重算
            // v0.15：读取快捷键采集框并重注册 —— 修饰键固定 Alt，只取主键
            //（留空 = 保持不变）
            LRESULT k1 = SendMessageW(GetDlgItem(h, IDC_HK_SHOW), HKM_GETHOTKEY, 0, 0);
            LRESULT k2 = SendMessageW(GetDlgItem(h, IDC_HK_CAPTURE), HKM_GETHOTKEY, 0, 0);
            UINT vk1 = LOWORD(k1), vk2 = LOWORD(k2);
            if (vk1) { g_hkShowVk = vk1; g_hkShowMod = MOD_ALT; }
            if (vk2) { g_hkCapVk  = vk2; g_hkCapMod  = MOD_ALT; }
            ApplyHotkeys();
            SaveTasks();
            EndDialog(h, IDOK);
            CheckReminders();            // 设置可能变化，立即复查
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        break;
    case WM_NCDESTROY:
        if (sBrTask) { DeleteObject(sBrTask); sBrTask = nullptr; }
        if (sBrGrp)  { DeleteObject(sBrGrp);  sBrGrp  = nullptr; }
        if (sBrDt)   { DeleteObject(sBrDt);   sBrDt   = nullptr; }
        if (sBrGrpDone) { DeleteObject(sBrGrpDone); sBrGrpDone = nullptr; }
        break;
    }
    return FALSE;
}

void OpenSettings() {
    // 状态自检行：同时让用户确认下载到了新版 + 托盘小图标当前状态
    //（版本号需与 resource.rc 的 ProductVersion 同步更新）
    wchar_t trayStat[128];
    if (!g_nidAdded)
        lstrcpynW(trayStat,
                  TICKLY_VER_PFX L" · 托盘小图标添加失败，5 秒后自动重试；若一直失败请重启资源管理器", 128);
    else if (g_trayRollback)
        lstrcpynW(trayStat,
                  TICKLY_VER_PFX L" · 图标已回退为系统默认：收纳在右下角「^」折叠区内，一定显示", 128);
    else if (g_trayPromoted)
        lstrcpynW(trayStat,
                  TICKLY_VER_PFX L" · 托盘小图标常驻任务栏右下角（智能心跳保活）", 128);
    else
        lstrcpynW(trayStat,
                  TICKLY_VER_PFX L" · 托盘小图标可能收纳在右下角「^」折叠区内（智能心跳保活）", 128);
    static std::wstring hkStat;               // 状态行持有的字符串（对话框生存期）
    hkStat = HotkeyStatusText();
    std::vector<DlgItem> items = {
        // v2.3：三个复选框 + 三个单选钮改 BS_OWNERDRAW（见 DrawDlgChoice 注释）
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
          12, 12, 206, 10, (WORD)IDC_CK_REMIND, CLS_BUTTON, L"到时间弹出提醒框" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 31,  26, 10, 100, CLS_STATIC, L"提前" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
          42, 29,  34, 12, (WORD)IDC_ED_MIN, CLS_EDIT, L"15" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          82, 31, 136, 10, 100, CLS_STATIC, L"分钟后提醒（0 = 到点即提醒）" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
          12, 49, 206, 10, (WORD)IDC_CK_AUTORUN, CLS_BUTTON, L"开机自动启动" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
          12, 67, 206, 10, (WORD)IDC_CK_TRAYRUN, CLS_BUTTON,
          L"关闭主窗口后在托盘后台运行（提醒继续生效）" },
        // v1.12.0 两个全局快捷键；v0.15 起固定 Alt 修饰 —— 采集框前显示
        // 不可修改的「Alt+」文本，采集框只录主键
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12,  89, 74, 10, 100, CLS_STATIC, L"显示主窗口：" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          88,  89, 22, 10, 100, CLS_STATIC, L"Alt+" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP,
          112,  86, 70, 13, (WORD)IDC_HK_SHOW, 0, L"", L"msctls_hotkey32" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 107, 74, 10, 100, CLS_STATIC, L"划词添加任务：" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          88, 107, 22, 10, 100, CLS_STATIC, L"Alt+" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP,
          112, 104, 70, 13, (WORD)IDC_HK_CAPTURE, 0, L"", L"msctls_hotkey32" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 126, 206, 10, 100, CLS_STATIC, trayStat },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 140, 206, 10, 100, CLS_STATIC, hkStat.c_str() },
        // v1.13.0：字体大小与颜色（任务内容字体 / 分组标题字体 / v0.26 日期
        // 时间字体，点击色块弹系统取色器自定义颜色）
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 162, 54, 10, 100, CLS_STATIC, L"任务字体：" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
          70, 160, 34, 12, (WORD)IDC_ED_TASKFONT, CLS_EDIT, L"14" },
        { WS_CHILD|WS_VISIBLE|SS_NOTIFY|WS_BORDER,
          112, 160, 34, 12, (WORD)IDC_SW_TASK, CLS_STATIC, L"" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          152, 162, 70, 10, 100, CLS_STATIC, L"点色块改颜色" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 181, 54, 10, 100, CLS_STATIC, L"未完成组：" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
          70, 179, 34, 12, (WORD)IDC_ED_GRPFONT, CLS_EDIT, L"12" },
        { WS_CHILD|WS_VISIBLE|SS_NOTIFY|WS_BORDER,
          112, 179, 34, 12, (WORD)IDC_SW_GROUP, CLS_STATIC, L"" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          152, 181, 70, 10, 100, CLS_STATIC, L"点色块改颜色" },
        // v1.13.6：已完成分组（本周 / 上周 / 本月 / 上月 / 其他）单独一套
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 200, 54, 10, 100, CLS_STATIC, L"已完成组：" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
          70, 198, 34, 12, (WORD)IDC_ED_GRPFONT_DONE, CLS_EDIT, L"12" },
        { WS_CHILD|WS_VISIBLE|SS_NOTIFY|WS_BORDER,
          112, 198, 34, 12, (WORD)IDC_SW_GROUP_DONE, CLS_STATIC, L"" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          152, 200, 70, 10, 100, CLS_STATIC, L"点色块改颜色" },
        // v0.26：日期时间字体（未完成任务行右侧的日期时间文字）
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 219, 54, 10, 100, CLS_STATIC, L"日期字体：" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_NUMBER,
          70, 217, 34, 12, (WORD)IDC_ED_DTFONT, CLS_EDIT, L"12" },
        { WS_CHILD|WS_VISIBLE|SS_NOTIFY|WS_BORDER,
          112, 217, 34, 12, (WORD)IDC_SW_DT, CLS_STATIC, L"" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          152, 219, 70, 10, 100, CLS_STATIC, L"点色块改颜色" },
        // v0.27：外观（主题三态）—— 浅色 / 深色 / 跟随系统，
        // 第一个单选钮带 WS_GROUP，切换立即生效并联存
        { WS_CHILD|WS_VISIBLE|SS_LEFT,
          12, 238, 34, 10, 100, CLS_STATIC, L"外观：" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW|WS_GROUP,
          48, 237, 44, 12, (WORD)IDC_RB_THEME_L, CLS_BUTTON, L"浅色" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
          96, 237, 44, 12, (WORD)IDC_RB_THEME_D, CLS_BUTTON, L"深色" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
          144, 237, 80, 12, (WORD)IDC_RB_THEME_S, CLS_BUTTON, L"跟随系统" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON|WS_GROUP,
          62, 253, 48, 14, IDOK, CLS_BUTTON, L"保存" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          118, 253, 48, 14, (WORD)IDCANCEL, CLS_BUTTON, L"取消" },
    };
    DLGTEMPLATE* tpl = BuildDialog(L"设置 · " TICKLY_VER_PFX, 230, 278, items);
    DialogBoxIndirectParam(GetModuleHandleW(nullptr), tpl, g_hwnd,
                           SettingsDlgProc, 0);
    std::free(tpl);
}
