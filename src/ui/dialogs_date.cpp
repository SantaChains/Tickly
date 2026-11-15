// 日期 / 完成日期对话框
#include "common.h"
#include "ui/dialogs_date.h"
#include "ui/dialogs_common.h"
#include "ui/dark_mode.h"
#include "ui/theme.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "ui/reminder.h"
#include "ui/context.h"
#include "app/store.h"
#include "core/task.h"
#include "core/time_util.h"
#include "ui/main_window.h"


// ---------------------------- 修改日期时间对话框 ----------------------------
static const int IDC_DP_DATE    = 2201;  // 日期选择器
static const int IDC_DP_TIME    = 2202;  // 时间选择器
static const int IDC_BTN_CLRDUE = 2203;  // 清除日期
static const int IDC_DP2_DATE   = 2205;  // v0.15 完成日期编辑：日期选择器
static const int IDC_DP2_TIME   = 2206;  // v0.15 完成日期编辑：时间选择器
// v2.3：日期 / 时间是否启用，改用真正的复选框 —— DTS_SHOWNONE 自带的勾选框
// 是 comctl32 内部按 96 DPI 画好再拉伸的小位图，高分屏下又小又糊
static const int IDC_CK_DATE    = 2207;
static const int IDC_CK_TIME    = 2208;

static INT_PTR CALLBACK DateDlgProc(HWND h, UINT m, WPARAM w, LPARAM lp) {
    switch (m) {
    case WM_CTLCOLORDLG:                 // 对话框背景 / 系统控件底色随主题
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)w, COL_TEXT);
        SetBkColor((HDC)w, m == WM_CTLCOLOREDIT ? COL_WHITE : COL_BG);
        return (INT_PTR)CachedBrush(m == WM_CTLCOLOREDIT ? COL_WHITE : COL_BG);
    case WM_INITDIALOG: {
        DarkModeApplyToWindow(h);   // 系统控件（DTP/复选/标题栏）随主题
        SetWindowLongPtrW(h, GWLP_USERDATA, lp);
        int idx = (int)lp;
        if (idx < 0 || idx >= (int)g_tasks.size()) return TRUE;
        const Task& t = g_tasks[(size_t)idx];
        HWND dDate = GetDlgItem(h, IDC_DP_DATE);
        HWND dTime = GetDlgItem(h, IDC_DP_TIME);
        if (!t.due) {
            // 无日期任务（点「＋ 日期」进入）：默认给出今天，时间不启用，可直接调整保存
            tm n = LocalTmOf((long long)time(nullptr));
            SYSTEMTIME today {};
            today.wYear  = (WORD)(n.tm_year + 1900);
            today.wMonth = (WORD)(n.tm_mon + 1);
            today.wDay   = (WORD)n.tm_mday;
            SendMessageW(dDate, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&today);
            SendMessageW(dTime, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&today);
            CheckDlgButton(h, IDC_CK_DATE, BST_CHECKED);
            CheckDlgButton(h, IDC_CK_TIME, BST_UNCHECKED);
        } else {
            tm d = LocalTmOf(t.due);
            SYSTEMTIME st {};
            st.wYear   = (WORD)(d.tm_year + 1900);
            st.wMonth  = (WORD)(d.tm_mon + 1);
            st.wDay    = (WORD)d.tm_mday;
            st.wHour   = (WORD)d.tm_hour;
            st.wMinute = (WORD)d.tm_min;
            SendMessageW(dDate, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
            SendMessageW(dTime, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
            CheckDlgButton(h, IDC_CK_DATE, BST_CHECKED);
            CheckDlgButton(h, IDC_CK_TIME,
                           t.hasTime ? BST_CHECKED : BST_UNCHECKED);
        }
        // v2.3：未勾选的时间选择器置灰，视觉上明确「不含具体时间」
        EnableWindow(dTime, IsDlgButtonChecked(h, IDC_CK_TIME) == BST_CHECKED);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            int idx = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
            if (idx >= 0 && idx < (int)g_tasks.size()) {
                Task& t = g_tasks[(size_t)idx];
                SYSTEMTIME sd {}, stt {};
                // v2.3：日期 / 时间是否启用由前面的复选框决定（DTP 不再带
                // DTS_SHOWNONE，DTM_GETSYSTEMTIME 恒为 GDT_VALID）
                bool hasD = IsDlgButtonChecked(h, IDC_CK_DATE) == BST_CHECKED;
                bool hasT = IsDlgButtonChecked(h, IDC_CK_TIME) == BST_CHECKED;
                SendMessageW(GetDlgItem(h, IDC_DP_DATE), DTM_GETSYSTEMTIME, 0,
                             (LPARAM)&sd);
                SendMessageW(GetDlgItem(h, IDC_DP_TIME), DTM_GETSYSTEMTIME, 0,
                             (LPARAM)&stt);
                if (hasD) {
                    int hh = hasT ? stt.wHour : 9;
                    int mm = hasT ? stt.wMinute : 0;
                    t.due      = MakeEpoch(sd.wYear, sd.wMonth, sd.wDay, hh, mm);
                    t.hasTime  = hasT;
                    t.notified = false;   // 时间变更后允许再次提醒
                } else {
                    t.due = 0;
                    t.hasTime = false;
                }
                RequestSave();
                RebuildRows();
                CheckReminders();         // 新时间若已进入提醒窗口则提示
            }
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (LOWORD(w) == IDC_BTN_CLRDUE) {
            CheckDlgButton(h, IDC_CK_DATE, BST_UNCHECKED);
            CheckDlgButton(h, IDC_CK_TIME, BST_UNCHECKED);
            EnableWindow(GetDlgItem(h, IDC_DP_DATE), FALSE);
            EnableWindow(GetDlgItem(h, IDC_DP_TIME), FALSE);
            return TRUE;
        }
        // v2.3：勾选/取消「日期」「时间」时启用或置灰对应选择器
        if (HIWORD(w) == BN_CLICKED &&
            (LOWORD(w) == IDC_CK_DATE || LOWORD(w) == IDC_CK_TIME)) {
            HWND dp = GetDlgItem(h, LOWORD(w) == IDC_CK_DATE ? IDC_DP_DATE
                                                             : IDC_DP_TIME);
            EnableWindow(dp, IsDlgButtonChecked(h, LOWORD(w)) == BST_CHECKED);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

// ---------------------------- 修改完成日期对话框（v0.15） --------------------
// 已完成任务右键「修改完成日期」/ 单击其日期标签：只改 doneAt（完成时间），
// 不影响任务原本设定的截止日期
static INT_PTR CALLBACK DoneDateDlgProc(HWND h, UINT m, WPARAM w, LPARAM lp) {
    switch (m) {
    case WM_INITDIALOG: {
        DarkModeApplyToWindow(h);
        SetWindowLongPtrW(h, GWLP_USERDATA, lp);
        int idx = (int)lp;
        if (idx < 0 || idx >= (int)g_tasks.size()) return TRUE;
        tm d = LocalTmOf(g_tasks[(size_t)idx].doneAt);
        SYSTEMTIME st {};
        st.wYear   = (WORD)(d.tm_year + 1900);
        st.wMonth  = (WORD)(d.tm_mon + 1);
        st.wDay    = (WORD)d.tm_mday;
        st.wHour   = (WORD)d.tm_hour;
        st.wMinute = (WORD)d.tm_min;
        SendMessageW(GetDlgItem(h, IDC_DP2_DATE), DTM_SETSYSTEMTIME,
                     GDT_VALID, (LPARAM)&st);
        SendMessageW(GetDlgItem(h, IDC_DP2_TIME), DTM_SETSYSTEMTIME,
                     GDT_VALID, (LPARAM)&st);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            int idx = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
            if (idx >= 0 && idx < (int)g_tasks.size()) {
                Task& t = g_tasks[(size_t)idx];
                SYSTEMTIME sd {}, stt {};
                bool hasD = SendMessageW(GetDlgItem(h, IDC_DP2_DATE),
                                         DTM_GETSYSTEMTIME, 0,
                                         (LPARAM)&sd) == GDT_VALID;
                bool hasT = SendMessageW(GetDlgItem(h, IDC_DP2_TIME),
                                         DTM_GETSYSTEMTIME, 0,
                                         (LPARAM)&stt) == GDT_VALID;
                if (hasD) {
                    // 时间未单独调整时保留原完成时刻的时分
                    tm old = LocalTmOf(t.doneAt);
                    int hh = hasT ? stt.wHour : old.tm_hour;
                    int mm = hasT ? stt.wMinute : old.tm_min;
                    t.doneAt = MakeEpoch(sd.wYear, sd.wMonth, sd.wDay, hh, mm);
                    RequestSave();
                    RebuildRows();
                }
            }
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

void OpenDoneDateEditor(int idx) {
    if (idx < 0 || idx >= (int)g_tasks.size()) return;
    std::vector<DlgItem> items = {
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 12, 15, 28, 10, 100, CLS_STATIC, L"日期" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|DTS_SHORTDATECENTURYFORMAT,
          46, 12, 96, 13, (WORD)IDC_DP2_DATE, 0, L"", L"SysDateTimePick32" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 12, 34, 28, 10, 100, CLS_STATIC, L"时间" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|DTS_TIMEFORMAT,
          46, 31, 96, 13, (WORD)IDC_DP2_TIME, 0, L"", L"SysDateTimePick32" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
          47, 56, 48, 14, IDOK, CLS_BUTTON, L"保存" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          119, 56, 48, 14, IDCANCEL, CLS_BUTTON, L"取消" },
    };
    DLGTEMPLATE* tpl = BuildDialog(L"修改完成日期", 214, 80, items);
    DialogBoxIndirectParam(GetModuleHandleW(nullptr), tpl, g_hwnd,
                           DoneDateDlgProc, (LPARAM)idx);
    std::free(tpl);
}

// 单击任务行的日期时间标签 -> 修改/清除日期时间
// v0.15：已完成任务显示的是完成日期 -> 转到「修改完成日期」对话框
void OpenDateEditor(int idx) {
    if (idx < 0 || idx >= (int)g_tasks.size()) return;
    if (g_tasks[(size_t)idx].done) { OpenDoneDateEditor(idx); return; }
    std::vector<DlgItem> items = {
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
          12, 15, 32, 10, (WORD)IDC_CK_DATE, CLS_BUTTON, L"日期" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|DTS_SHORTDATECENTURYFORMAT,
          46, 12, 96, 13, (WORD)IDC_DP_DATE, 0, L"", L"SysDateTimePick32" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
          12, 34, 32, 10, (WORD)IDC_CK_TIME, CLS_BUTTON, L"时间" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|DTS_TIMEFORMAT,
          46, 31, 96, 13, (WORD)IDC_DP_TIME, 0, L"", L"SysDateTimePick32" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          12, 56, 52, 14, (WORD)IDC_BTN_CLRDUE, CLS_BUTTON, L"清除日期" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
          100, 56, 48, 14, IDOK, CLS_BUTTON, L"保存" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          154, 56, 48, 14, (WORD)IDCANCEL, CLS_BUTTON, L"取消" },
    };
    DLGTEMPLATE* tpl = BuildDialog(L"修改日期和时间", 214, 80, items);
    DialogBoxIndirectParam(GetModuleHandleW(nullptr), tpl, g_hwnd,
                           DateDlgProc, (LPARAM)idx);
    std::free(tpl);
}
