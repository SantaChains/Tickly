// 任务提醒
#include "common.h"
#include "ui/reminder.h"
#include "ui/dialogs_common.h"
#include "ui/dark_mode.h"
#include "ui/theme.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "app/ids.h"
#include "ui/context.h"
#include "app/store.h"
#include "core/task.h"
#include "core/time_util.h"
#include "ui/main_window.h"

bool g_remindOn   = true;      // 到点提醒开关（持久化）
int  g_remindMin  = 15;        // 提前提醒分钟数（持久化）
static bool g_remindOpen = false;     // 提醒弹窗正在显示
// v0.15 提醒弹窗「完成」按钮：本次提醒的任务清单（用 due+text 定位，
// 避免弹窗期间任务增删导致下标漂移）
static std::vector<std::pair<long long, std::wstring>> g_remindItems;

// ---------------------------- 任务提醒 --------------------------------------
static std::wstring RelTime(long long due, long long now) {
    long long d = due - now;
    if (d <= 0) return L"已到时间";
    if (d < 60) return L"1 分钟内";
    long long mi = (d + 59) / 60;
    if (mi < 60) return L"还有 " + std::to_wstring(mi) + L" 分钟";
    long long hh = mi / 60, mm = mi % 60;
    std::wstring s = L"还有 " + std::to_wstring(hh) + L" 小时";
    if (mm) s += L" " + std::to_wstring(mm) + L" 分钟";
    return s;
}

static INT_PTR CALLBACK ReminderDlgProc(HWND h, UINT m, WPARAM w, LPARAM) {
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
        DarkModeApplyToWindow(h);
        // 大号加粗字体（正文 17px / 按钮 13px，均 FW_BOLD）
        SendMessageW(GetDlgItem(h, 100), WM_SETFONT, (WPARAM)g_fontRemindT, TRUE);
        SendMessageW(GetDlgItem(h, IDOK), WM_SETFONT, (WPARAM)g_fontRemindB, TRUE);
        SendMessageW(GetDlgItem(h, IDC_REMIND_DONE), WM_SETFONT,
                     (WPARAM)g_fontRemindB, TRUE);
        // v0.19：正文按实际字体与控件宽度重新测量并放大窗口 —— 模板高度按
        // 「每条 2 行」估算，长任务预览（22 字接近满宽）或多条提醒、文本含
        // 换行时会折成 3 行以上，底部内容被控件或窗口边缘截断；实测后放大
        // 窗口、按钮组随正文底部下移，长文本提醒框也完整显示全部内容
        GrowDialogToFitText(h, 100, { IDOK, IDC_REMIND_DONE });
        // v0.15：提醒框默认显示在屏幕正中央（主窗口所在显示器工作区居中）
        {
            HMONITOR mon = MonitorFromWindow(g_hwnd ? g_hwnd : h,
                                             MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi {};
            mi.cbSize = sizeof(mi);
            if (mon && GetMonitorInfoW(mon, &mi)) {
                RECT r {};
                GetWindowRect(h, &r);
                int nx = mi.rcWork.left +
                         ((mi.rcWork.right - mi.rcWork.left) - (r.right - r.left)) / 2;
                int ny = mi.rcWork.top +
                         ((mi.rcWork.bottom - mi.rcWork.top) - (r.bottom - r.top)) / 2;
                SetWindowPos(h, nullptr, nx, ny, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            }
        }
        // 全局弹窗：置顶并强制抢占前台 —— 即使主窗口已缩到托盘/在后台，
        // 也能立即浮到所有窗口之上（Windows 对后台进程调用
        // SetForegroundWindow 会静默失败，AttachThreadInput 可绕过该限制）
        DWORD myT = GetCurrentThreadId();
        HWND  hFG = GetForegroundWindow();
        DWORD fgT = hFG ? GetWindowThreadProcessId(hFG, nullptr) : 0;
        if (fgT && fgT != myT) AttachThreadInput(myT, fgT, TRUE);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(h);
        if (fgT && fgT != myT) AttachThreadInput(myT, fgT, FALSE);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) { EndDialog(h, IDOK); return TRUE; }
        if (LOWORD(w) == IDC_REMIND_DONE) {   // v0.15：一键完成本次提醒的任务
            long long now = (long long)time(nullptr);
            for (const auto& it : g_remindItems) {
                for (size_t i = 0; i < g_tasks.size(); ++i) {
                    Task& t = g_tasks[i];
                    if (!t.done && t.due == it.first && t.text == it.second) {
                        t.done   = true;
                        t.doneAt = now;
                        break;
                    }
                }
            }
            g_remindItems.clear();
            RequestSave();
            RebuildRows();
            EndDialog(h, IDC_REMIND_DONE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void CheckReminders() {
    if (!g_remindOn || g_remindOpen) return;
    long long now = (long long)time(nullptr);
    long long adv = (long long)g_remindMin * 60;
    std::vector<int> cand;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        const Task& t = g_tasks[i];
        if (t.done || !t.due || t.notified) continue;
        if (now >= t.due - adv) cand.push_back((int)i);
    }
    if (cand.empty()) return;
    std::sort(cand.begin(), cand.end(), [](int a, int b) {
        return g_tasks[(size_t)a].due < g_tasks[(size_t)b].due;
    });
    std::wstring msg;
    g_remindItems.clear();
    for (int i : cand) {
        Task& t = g_tasks[(size_t)i];
        t.notified = true;               // 先标记：每个任务只提醒一次
        g_remindItems.emplace_back(t.due, t.text);   // v0.15「完成」按钮用
        bool od = false;
        std::wstring when = FormatWhen(t.due, t.hasTime, false, &od);
        std::wstring txt = (t.text.size() > 22) ? t.text.substr(0, 22) + L"…"
                                                : t.text;
        for (auto& c : txt)                  // v0.19：预览压平换行 —— 多行任务
            if (c == L'\n' || c == L'\r') c = L' ';   // 的预览保持单行结构
        if (!msg.empty()) msg += L"\r\n";
        msg += L"· " + txt + L"\r\n    " + when + L"（" + RelTime(t.due, now)
             + L"）";
    }
    RequestSave();
    g_remindOpen = true;
    MessageBeep(MB_ICONEXCLAMATION);
    int textH = (int)cand.size() * 26 + 6;   // 每条 2 行（按大号加粗字体计高）
    std::vector<DlgItem> items = {
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 14, 12, 216, (WORD)textH, 100,
          CLS_STATIC, msg.c_str() },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
          54, (WORD)(12 + textH + 10), 64, 16, IDOK, CLS_BUTTON, L"知道了" },
        // v0.15：右侧「完成」按钮 —— 一键把本次提醒的任务标记为完成
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          126, (WORD)(12 + textH + 10), 64, 16, (WORD)IDC_REMIND_DONE,
          CLS_BUTTON, L"完成" },
    };
    // WS_EX_TOPMOST：全局置顶弹窗，应用在后台/托盘时也会盖过其他窗口弹出
    DLGTEMPLATE* tpl = BuildDialog(L"任务提醒", 244, 12 + textH + 10 + 16 + 12,
                                   items, WS_EX_TOPMOST);
    DialogBoxIndirectParam(GetModuleHandleW(nullptr), tpl, g_hwnd,
                           ReminderDlgProc, 0);
    std::free(tpl);
    g_remindOpen = false;
}
