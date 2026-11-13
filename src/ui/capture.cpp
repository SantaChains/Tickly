// 划词添加任务
#include "common.h"
#include "ui/capture.h"
#include "ui/dialogs_common.h"
#include "ui/dark_mode.h"
#include "ui/theme.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "ui/list_view.h"
#include "core/date_parse.h"
#include "ui/reminder.h"
#include "ui/context.h"
#include "app/store.h"
#include "core/task.h"
#include "core/text_util.h"
#include "core/time_util.h"
#include "ui/toast.h"
#include "ui/main_window.h"


// ---------------------------- 划词添加确认对话框（v0.15） --------------------
// 所选文本识别到日期时间时弹出：显示识别结果与任务内容，确认后按
// 识别到的日期时间入列（确认返回 true / 取消返回 false）。
// v0.20：识别结果可在弹窗内手动修改（日期/时间选择器），确认后按修改
// 后的值入列（通过 CapConfirmCtx 回传）。
// v0.21：任务内容同样可在弹窗内编辑（多行输入框，预填完整识别内容），
// 确认后按编辑后的内容入列（CapConfirmCtx::text 回传）。
// 对话框模板 DlgItem/BuildDialog 定义在「内存对话框模板」一节，
// ConfirmCapturedWhen 定义在其后，此处先前置声明供划词流程调用
struct CapConfirmCtx {
    long long due     = 0;    // 入列日期时间（识别值，可被弹窗修改）
    bool      hasTime = false;
    std::wstring text;        // v0.21：入列内容（弹窗内可编辑，确认时回传）
};
static const int IDC_DP3_DATE = 2207;  // v0.20 划词确认框：日期选择器
static const int IDC_DP3_TIME = 2208;  // v0.20 划词确认框：时间选择器
static bool ConfirmCapturedWhen(const std::wstring& whenStr,
                                const std::wstring& content,
                                CapConfirmCtx& ctx);

static INT_PTR CALLBACK CapConfirmDlgProc(HWND h, UINT m, WPARAM w, LPARAM lp) {
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
        SetWindowLongPtrW(h, GWLP_USERDATA, lp);
        // v0.20：识别到的日期时间预填到选择器，用户可直接修改（时间
        // 选择器带 DTS_SHOWNONE 复选框：未勾选 = 不带具体时间，与
        // 识别结果一致；勾选后可指定时:分）
        if (auto* ctx = (CapConfirmCtx*)lp) {
            tm d = LocalTmOf(ctx->due);
            SYSTEMTIME st {};
            st.wYear   = (WORD)(d.tm_year + 1900);
            st.wMonth  = (WORD)(d.tm_mon + 1);
            st.wDay    = (WORD)d.tm_mday;
            st.wHour   = (WORD)d.tm_hour;
            st.wMinute = (WORD)d.tm_min;
            SendMessageW(GetDlgItem(h, IDC_DP3_DATE), DTM_SETSYSTEMTIME,
                         GDT_VALID, (LPARAM)&st);
            HWND dTime = GetDlgItem(h, IDC_DP3_TIME);
            SendMessageW(dTime, DTM_SETSYSTEMTIME,
                         ctx->hasTime ? GDT_VALID : GDT_NONE, (LPARAM)&st);
            SendMessageW(dTime, DTM_SETFORMATW, 0, (LPARAM)L"H:mm");
        }
        // v0.19：任务内容可能折行超模板估算，先按实测换行高度放大窗口、
        // 按钮组随正文底部下移；v0.21 内容改为可编辑输入框 —— 高度上限
        // 约 7 行（窗口不无限加高），更长的内容在框内滚动查看
        int capH = 0;
        if (HWND ed = GetDlgItem(h, 102)) {
            HDC dc = GetDC(ed);
            if (dc) {
                HFONT f = (HFONT)SendMessageW(ed, WM_GETFONT, 0, 0);
                HGDIOBJ of = SelectObject(dc, f ? (HGDIOBJ)f
                                   : GetStockObject(DEFAULT_GUI_FONT));
                TEXTMETRICW tmc {};
                if (GetTextMetricsW(dc, &tmc))
                    capH = (tmc.tmHeight + tmc.tmExternalLeading) * 7 + 12;
                SelectObject(dc, of);
                ReleaseDC(ed, dc);
            }
        }
        GrowDialogToFitText(h, 102, { IDOK, IDCANCEL }, capH, true);
        // 热键触发时主窗口可能在后台/托盘：把确认框带到前台（同提醒弹窗）
        DWORD myT = GetCurrentThreadId();
        HWND  hFG = GetForegroundWindow();
        DWORD fgT = hFG ? GetWindowThreadProcessId(hFG, nullptr) : 0;
        if (fgT && fgT != myT) AttachThreadInput(myT, fgT, TRUE);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(h);
        if (fgT && fgT != myT) AttachThreadInput(myT, fgT, FALSE);
        // v0.21：初始焦点给任务内容编辑框，可直接修改（Enter 确认 / Esc 取消）
        SetFocus(GetDlgItem(h, 102));
        return FALSE;                        // 已自行设置焦点
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            // v0.21：先读回（可能被编辑过的）任务内容 —— \r\n 归一为 \n、
            // 去首尾空白；内容被清空视为取消，不入列
            HWND ed = GetDlgItem(h, 102);
            int  tl = GetWindowTextLengthW(ed);
            std::wstring txt((size_t)(tl > 0 ? tl : 0) + 1, L'\0');
            if (tl > 0) GetWindowTextW(ed, &txt[0], tl + 1);
            txt.resize((size_t)(tl > 0 ? wcslen(txt.c_str()) : 0));
            std::wstring content;
            content.reserve(txt.size());
            for (wchar_t c : txt)
                if (c != L'\r') content += c;
            size_t b = content.find_first_not_of(L" \t\n");
            if (b == std::wstring::npos) {   // 清空 = 取消
                EndDialog(h, IDCANCEL);
                return TRUE;
            }
            size_t ce = content.find_last_not_of(L" \t\n");
            content = content.substr(b, ce - b + 1);
            // v0.20：确认时读取选择器 —— 日期恒有效（弹窗仅在识别到
            // 日期时出现）；时间未勾选则不带具体时间（入列当天 0 点）
            if (auto* ctx = (CapConfirmCtx*)GetWindowLongPtrW(h, GWLP_USERDATA)) {
                SYSTEMTIME sd {}, stt {};
                bool hasD = SendMessageW(GetDlgItem(h, IDC_DP3_DATE),
                                         DTM_GETSYSTEMTIME, 0,
                                         (LPARAM)&sd) == GDT_VALID;
                bool hasT = SendMessageW(GetDlgItem(h, IDC_DP3_TIME),
                                         DTM_GETSYSTEMTIME, 0,
                                         (LPARAM)&stt) == GDT_VALID;
                if (hasD)
                    ctx->due = MakeEpoch(sd.wYear, sd.wMonth, sd.wDay,
                                         hasT ? stt.wHour   : 0,
                                         hasT ? stt.wMinute : 0);
                ctx->hasTime = hasT;
                ctx->text    = content;      // v0.21：编辑后的内容回传
            }
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

// ---------------------------- 划词添加任务（v1.12.0） ------------------------
// 全局快捷键触发：向前台应用模拟 Ctrl+C 捕获选中文本，加入任务列表并弹气泡。
// 说明：捕获会占用剪贴板（与用户手动 Ctrl+C 后的剪贴板状态一致）
void CaptureSelectedToTask() {
    // 主窗口（或其子控件）在前台时不捕获，避免把界面文字捕获给自己
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_hwnd || GetAncestor(fg, GA_ROOT) == g_hwnd) {
        MessageBeep(MB_ICONWARNING);
        ShowToast(L"未捕获到选中文本", L"请先在其他应用中选中文字，再按快捷键", false);
        return;
    }
    DWORD seq0 = GetClipboardSequenceNumber();
    // v0.28 记事本取词修复 —— 先试零注入的直接复制：
    // Alt 系热键触发时前台 Win32 程序已收到 Alt 按下，为让 Ctrl+C 干净
    // 而注入的 Alt 弹起会让经典记事本这类标准菜单程序进入菜单模态循环
    //（Alt 弹起 → SC_KEYMENU 是 Win32 菜单激活的标准路径），后续注入
    // 的 Ctrl+C 全被菜单循环吞掉，剪贴板永不变化。而 Win32 编辑控件
    //（类名含 Edit/RichEdit，记事本编辑区、写字板、Scintilla 系编辑器
    // 均如此）收到 WM_COPY 会直接把选中文本放入剪贴板 —— 这正是
    // Ctrl+C 在编辑控件内部的实现。因此对类名匹配的前台焦点控件直接发
    // WM_COPY（SendMessageTimeout 防目标挂起时阻塞自己），全程不注入
    // 按键；未命中再走下方通用组合键注入。
    bool changed = false;
    {
        GUITHREADINFO gi { sizeof(gi) };
        DWORD fgTid = GetWindowThreadProcessId(fg, nullptr);
        if (fgTid && GetGUIThreadInfo(fgTid, &gi) && gi.hwndFocus) {
            wchar_t cls[96] {};
            GetClassNameW(gi.hwndFocus, cls, 96);
            std::wstring lc = cls;
            CharLowerBuffW(lc.data(), (DWORD)lc.size());
            if (lc.find(L"edit") != std::wstring::npos ||
                lc.find(L"scintilla") != std::wstring::npos) {
                DWORD_PTR r = 0;
                SendMessageTimeoutW(gi.hwndFocus, WM_COPY, 0, 0,
                                    SMTO_ABORTIFHUNG, 300, &r);
                for (int i = 0; i < 16; ++i) {       // 最多约 240ms
                    if (i) Sleep(15);
                    if (GetClipboardSequenceNumber() != seq0) {
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
    // v0.18：不再等待修饰键物理松开（原最多 1.5 秒：热键在 Alt 按下瞬间
    // 触发，这段等待几乎每次都吃满到用户抬手，是弹窗延迟的大头）。
    // v1.12.1 已确认「注入修饰键弹起」才是确定性修复 —— 下面每次尝试的
    // 组合键都以同一批 SendInput 先注入 Alt/Shift/Win 弹起，到达目标
    // 应用的始终是干净的复制组合键；热键以 MOD_NOREPEAT 注册且在按下
    // 瞬间触发，Alt 的自动重复（≥500ms 后才开始）来不及插进注入序列
    // 向前台应用发送复制组合键（v1.13.0 Chrome 修复）：
    //  · 每个键之间留 25~45ms 间隔 —— Chromium 的输入管线对零间隔连发的
    //    Ctrl+C 偶尔丢键（记事本宽容，浏览器主进程 accelerator 需要完整
    //    的按下/抬起序列），逐键发送 + 间隔是各类划词工具的通用做法
    //  · 附带扫描码（wScan），个别按虚拟键过滤输入的应用也能收到
    //  · 最多 3 次尝试：Ctrl+C -> Ctrl+Insert -> Ctrl+C（第 2 次换备用
    //    复制快捷键，Chrome 对 Ctrl+Insert 同样响应）
    auto sendKey = [](WORD vk, bool up) {
        INPUT in {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        if (up) in.ki.dwFlags |= KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    };
    auto copyCombo = [&](bool insertInstead) {
        // v0.18：修饰键弹起与 Ctrl 按下合并为同一批 SendInput —— 弹起与
        // 组合键之间零间隙，物理上仍按住的 Alt/Shift 无法混进组合键；
        // Ctrl 按下与字符键按下之间仍保留 30ms 间隔（Chrome 修复需要）
        {
            INPUT b[4] = {};
            b[0].type = INPUT_KEYBOARD; b[0].ki.wVk = VK_MENU;
            b[0].ki.dwFlags = KEYEVENTF_KEYUP;
            b[1].type = INPUT_KEYBOARD; b[1].ki.wVk = VK_SHIFT;
            b[1].ki.dwFlags = KEYEVENTF_KEYUP;
            b[2].type = INPUT_KEYBOARD; b[2].ki.wVk = VK_LWIN;
            b[2].ki.dwFlags = KEYEVENTF_KEYUP;
            b[3].type = INPUT_KEYBOARD; b[3].ki.wVk = VK_CONTROL;
            SendInput(4, b, sizeof(INPUT));
        }
        Sleep(30);
        sendKey(insertInstead ? VK_INSERT : L'C', false); Sleep(45);
        sendKey(insertInstead ? VK_INSERT : L'C', true);  Sleep(30);
        sendKey(VK_CONTROL, true);  Sleep(25);
    };
    if (!changed) {                    // WM_COPY 未命中才注入复制组合键
        for (int attempt = 0; attempt < 3 && !changed; ++attempt) {
            copyCombo(attempt == 1);   // 第 2 次：Ctrl+Insert 备用快捷键
            // v0.18：组合键发出后立即检查（多数应用在 C 键按下后几十毫秒内
            // 就完成复制，往往早于组合键序列结束），未变化再每 15ms 轮询，
            // 每次尝试最多约 0.75 秒（原 40ms 一查，最快也要白等 40ms）
            for (int i = 0; i < 50; ++i) {
                if (i) Sleep(15);
                if (GetClipboardSequenceNumber() != seq0) {
                    changed = true;
                    break;
                }
            }
        }
    }
    if (!changed) {
        MessageBeep(MB_ICONWARNING);
        ShowToast(L"未捕获到选中文本", L"请先在其他应用中选中文字，再按快捷键", false);
        return;
    }
    // 读取剪贴板文本（目标应用可能短暂占用剪贴板，重试开板）
    std::wstring raw;
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(g_hwnd)) {
            HANDLE hd = GetClipboardData(CF_UNICODETEXT);
            if (hd) {
                PCWSTR p = (PCWSTR)GlobalLock(hd);
                if (p) {
                    raw.assign(p);
                    GlobalUnlock(hd);
                }
            }
            CloseClipboard();
            break;
        }
        Sleep(50);
    }
    std::wstring text = NormalizeMultiline(raw);
    if (text.empty()) {
        MessageBeep(MB_ICONWARNING);
        ShowToast(L"未捕获到选中文本", L"所选内容不是文本（或内容为空）", false);
        return;
    }
    // v0.15：识别日期时间 —— 识别到时先弹窗确认，确认后按该日期时间入列。
    // v0.16：多行文本也识别（逐行扫描、取最优行），修复腾讯会议邀请函
    // 「会议时间：2026/10/14 14:30-15:30」识别不到的问题
    {
        WhenResult wr;
        std::wstring addText;
        bool recognized = false;
        if (text.find(L'\n') == std::wstring::npos) {
            wr = ParseWhen(text);
            if (wr.ok) {
                recognized = true;
                addText = !wr.stripped.empty() ? wr.stripped : text;
            }
        } else {
            std::wstring stripped;
            if (ParseWhenMultiline(text, wr, stripped)) {
                recognized = true;
                addText = !stripped.empty() ? stripped : text;
            }
        }
        if (recognized) {
            std::wstring whenStr = FormatWhen(wr.due, wr.hasTime, false, nullptr);
            // v0.21：完整任务内容传入确认框（多行保留换行），弹窗内可直接
            // 编辑；确认后按编辑过的内容入列，气泡同步展示编辑结果
            CapConfirmCtx ctx;
            ctx.due     = wr.due;
            ctx.hasTime = wr.hasTime;
            if (!ConfirmCapturedWhen(whenStr, addText, ctx))
                return;                     // 取消：不入列
            std::wstring finalText = NormalizeMultiline(ctx.text);
            if (finalText.empty()) return;  // 内容被清空 = 取消（弹窗已拦）
            Task t;
            t.text    = finalText;
            t.due     = ctx.due;
            t.hasTime = ctx.hasTime;
            g_tasks.insert(g_tasks.begin(), t);   // 新任务置顶
            g_multiSel.clear();                   // 下标整体 +1，多选失效
            SaveTasks();
            RebuildRows();
            UpdateButtons();
            CheckReminders();
            MessageBeep(MB_OK);
            // v0.20：气泡展示确认框内（可能被修改过的）日期时间
            std::wstring body = finalText;
            for (auto& c : body)              // 气泡单行展示：换行压平
                if (c == L'\n') c = L' ';
            if (body.size() > 80) body = body.substr(0, 80) + L"…";
            body += L"（" + FormatWhen(ctx.due, ctx.hasTime, false, nullptr) +
                    L"）";
            ShowToast(L"已添加到 Tickly", body, true);
            return;
        }
    }
    Task t;
    t.text = text;   // 所选即所得：无日期时间表达时不改动内容
    g_tasks.insert(g_tasks.begin(), t);   // 新任务置顶
    g_multiSel.clear();                   // 下标整体 +1，多选失效
    SaveTasks();
    RebuildRows();
    UpdateButtons();
    CheckReminders();
    std::wstring body = (text.size() > 80) ? text.substr(0, 80) + L"…" : text;
    MessageBeep(MB_OK);
    ShowToast(L"已添加到 Tickly", body, true);
}

// ---------------------------- 划词添加确认对话框（v0.15） --------------------
// 定义在 BuildDialog 之后（前置声明在划词添加流程处）：
// 确认返回 true（按确认时的日期时间入列），取消返回 false（不入列）。
// v0.20：弹窗内日期/时间选择器预填识别结果，可手动修改后确认
// v0.21：任务内容预填到多行编辑框，可直接修改后确认（content = 完整内容）
static bool ConfirmCapturedWhen(const std::wstring& whenStr,
                                const std::wstring& content,
                                CapConfirmCtx& ctx) {
    std::wstring editInit = ToCrLf(content);   // EDIT 只认 \r\n 换行
    std::vector<DlgItem> items = {
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 12, 12, 208, 10, 100,
          CLS_STATIC, L"从所选文本识别到日期时间（可修改）：" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 24, 26, 196, 10, 101,
          CLS_STATIC, whenStr.c_str() },
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 12, 43, 26, 10, 100,
          CLS_STATIC, L"日期" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|DTS_SHORTDATECENTURYFORMAT,
          40, 40, 88, 13, (WORD)IDC_DP3_DATE, 0, L"", L"SysDateTimePick32" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 134, 43, 26, 10, 100,
          CLS_STATIC, L"时间" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|DTS_SHOWNONE|DTS_TIMEFORMAT,
          162, 40, 58, 13, (WORD)IDC_DP3_TIME, 0, L"", L"SysDateTimePick32" },
        { WS_CHILD|WS_VISIBLE|SS_LEFT, 12, 60, 208, 10, 100,
          CLS_STATIC, L"将添加的任务（可修改）：" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_MULTILINE|
          ES_AUTOVSCROLL|WS_VSCROLL, 24, 74, 196, 10, 102,
          CLS_EDIT, editInit.c_str() },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
          48, 98, 62, 14, IDOK, CLS_BUTTON, L"确认添加" },
        { WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          122, 98, 62, 14, IDCANCEL, CLS_BUTTON, L"取消" },
    };
    DLGTEMPLATE* tpl = BuildDialog(L"确认添加任务", 232, 122, items);
    // v0.17：确认框改为无属主弹出 —— 此前以主窗口为属主，确认框被激活时
    // 属主链一并激活/提升，托盘或后台的主界面会跟着弹到前台；快捷键划词
    // 场景下用户只想看到确认框本身，主界面应保持原状
    INT_PTR r = DialogBoxIndirectParam(GetModuleHandleW(nullptr), tpl,
                                       nullptr, CapConfirmDlgProc,
                                       (LPARAM)&ctx);
    std::free(tpl);
    return r == IDOK;
}
