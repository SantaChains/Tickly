// 行内编辑与自绘光标
#include "common.h"
#include "ui/row_editor.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "app/ids.h"
#include "ui/input_box.h"
#include "ui/list_view.h"
#include "core/date_parse.h"
#include "ui/context.h"
#include "app/store.h"
#include "core/task.h"
#include "core/text_util.h"
#include "ui/theme.h"
#include "ui/main_window.h"


// 行内编辑 / 右键删除
HWND    g_rowEdit        = nullptr;   // 行内编辑框
int     g_rowEditRow     = -1;        // 正在编辑的显示行
int     g_rowEditIdx     = -1;        // v0.17：正在编辑的任务下标（行高随编辑内容生长）
int     g_rowEditLines   = 0;         // v0.17：编辑框当前内容行数（0 = 未在编辑）
static WNDPROC g_oldRowEditProc = nullptr;
int     g_menuRow        = -1;        // 右键菜单对应任务行

// 关闭行内编辑框（save=true 保存修改；文本不变则跳过写盘）
void CloseRowEditor(bool save) {
    if (!g_rowEdit) return;
    HWND ed = g_rowEdit;
    int  row = g_rowEditRow;
    g_rowEdit      = nullptr;
    g_rowEditRow   = -1;
    g_rowEditIdx   = -1;   // v0.17：编辑态行高覆盖一并解除
    g_rowEditLines = 0;
    if (save && row >= 0 && row < (int)g_rows.size() && g_rows[row] >= 0) {
        int len = GetWindowTextLengthW(ed);
        std::wstring buf;
        buf.resize((size_t)len + 1);
        GetWindowTextW(ed, &buf[0], len + 1);
        buf.resize(wcslen(buf.c_str()));
        std::wstring text = NormalizeMultiline(buf);
        Task& t = g_tasks[(size_t)g_rows[row]];
        if (!text.empty() && text != t.text) {
            // 编辑文本中若写了日期时间词（如「明天 15:00」）则一并识别为任务属性
            //（仅单行文本参与识别；多行文本整段保留，不做日期解析）
            if (text.find(L'\n') == std::wstring::npos) {
                WhenResult w = ParseWhen(text);
                if (w.ok && !w.stripped.empty()) {
                    t.text     = w.stripped;
                    t.due      = w.due;
                    t.hasTime  = w.hasTime;
                    t.notified = false;      // 时间变更后允许再次提醒
                } else {
                    t.text = text;
                }
            } else {
                t.text = text;
            }
            SaveTasks();
            RefreshListKeepSel();   // v1.12.0：文本变了行高可能变，重建量高
        }
    }
    SetWindowLongPtrW(ed, GWLP_WNDPROC, (LONG_PTR)g_oldRowEditProc);
    DestroyWindow(ed);
    UpdateButtons();
    // v1.13.4：无条件重建行 —— 编辑态行高是「视口满高」，Esc/未改动关闭时
    // 若不重建，g_rowTops 会一直停在编辑态高度，整行留出一大块空白
    //（此前只有「文本变了」的分支才重建）
    RefreshListKeepSel();
    InvalidateRect(g_list, nullptr, TRUE);
    if (g_input) SetFocus(g_input);
}

// ---------------------------- 强调色光标（自绘 caret，v1.13.1） -----------------
// Win32 的 EDIT 光标颜色取自系统设置（默认黑），没有可改色的公开 API：
// CreateCaret 的位图是单色掩码（只能「反色 / 屏幕色」），SPI_SETCARETWIDTH 只
// 管宽度。这里隐藏系统光标，在插入点位置自绘一条强调色竖线（v2.3.2 起跟随
// COL_ACCENT），定时器维持 530ms 的闪烁节奏（与系统光标速率一致）。竖线随控件
// 重绘重新落位，因此每次相位切换都整框重绘一次 —— 编辑框只有几行文字，
// 代价可忽略。
static HWND     g_caretHwnd  = nullptr;   // 当前接管光标的编辑框
static UINT_PTR g_caretTimer = 0;         // 闪烁定时器（编辑框内 id 3）
static bool     g_caretOn    = true;      // 闪烁相位

// 插入点竖线矩形（客户区坐标）；无焦点 / 光标滚出可视区时返回空矩形
static RECT BlueCaretRect(HWND h) {
    RECT none { 0, 0, 0, 0 };
    if (!h || !IsWindow(h) || GetFocus() != h) return none;
    DWORD s0 = 0, s1 = 0;
    SendMessageW(h, EM_GETSEL, (WPARAM)&s0, (LPARAM)&s1);
    // v1.13.15：取选区终点 —— 普通编辑时 s0==s1 无差别；调用中文输入法组词
    // 期间 s0 落在「组合串开头」，用它定位会把蓝线画到刚敲的第一个字前面，
    // 用户看到的正是「光标跑到『后』字前面」。s1 是组合串末尾，与系统插入点
    // 的实际视觉位置一致
    s0 = s1;
    int x = 0, y = 0;
    bool located = false;
    LRESULT p = SendMessageW(h, EM_POSFROMCHAR, (WPARAM)(int)s0, 0);
    if (p != -1) {
        x = (int)(short)LOWORD(p);
        y = (int)(short)HIWORD(p);
        located = true;
    } else if (s0 > 0) {
        // v1.13.15：插入点在文本末尾时 EM_POSFROMCHAR 常返回 -1。此前这里用
        // GetCaretPos 兜底 —— 但系统 caret 已被 HideCaret 压住，该函数返回的
        // 是「线程 caret」的位置，往往是 (0,0)，蓝线就被画到了文本开头，
        // 用户看到的正是「光标跑到最前面」。改为用前一个字符的位置 + 该字符
        // 实测宽度推算插入点，纯几何计算，不依赖任何全局状态
        LRESULT q = SendMessageW(h, EM_POSFROMCHAR, (WPARAM)(int)(s0 - 1), 0);
        if (q != -1) {
            x = (int)(short)LOWORD(q);
            y = (int)(short)HIWORD(q);
            int adv = 0;
            std::wstring all;
            int n = GetWindowTextLengthW(h);
            if (n > 0) {
                all.resize((size_t)n + 1);
                GetWindowTextW(h, &all[0], n + 1);
                all.resize(wcslen(all.c_str()));
            }
            if ((int)s0 - 1 < (int)all.size()) {
                wchar_t ch[3] = { 0, 0, 0 };
                ch[0] = all[(size_t)s0 - 1];
                int cnt = 1;
                if (ch[0] >= 0xD800 && ch[0] <= 0xDBFF &&
                    (int)s0 < (int)all.size()) {
                    ch[1] = all[(size_t)s0];
                    cnt = 2;                 // 代理对：整个字符一起测
                }
                HDC hdx = GetDC(h);
                HFONT fx = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
                HGDIOBJ ofx = fx ? SelectObject(hdx, fx) : nullptr;
                SIZE szx {};
                if (GetTextExtentPoint32W(hdx, ch, cnt, &szx)) adv = (int)szx.cx;
                if (ofx) SelectObject(hdx, ofx);
                ReleaseDC(h, hdx);
            }
            x += adv;
            located = true;
        }
    }
    if (!located && s0 == 0) {
        // 首字符且定位失败：格式矩形左上角就是插入点（CenterInput 设过的内边距）
        RECT fr {};
        if (SendMessageW(h, EM_GETRECT, 0, (LPARAM)&fr)) {
            x = fr.left;
            y = fr.top;
            located = true;
        }
    }
    if (!located) return none;           // 宁可这帧不画，也不能画错位置
    RECT crc {};
    if (!GetClientRect(h, &crc) || crc.bottom <= 0 || crc.right <= 0) return none;
    HDC hdc = GetDC(h);
    HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HGDIOBJ of = f ? SelectObject(hdc, f) : nullptr;
    TEXTMETRICW tm {};
    GetTextMetricsW(hdc, &tm);
    if (of) SelectObject(hdc, of);
    ReleaseDC(h, hdc);
    int lh = (tm.tmHeight > 0) ? (int)tm.tmHeight : Scale(16);
    int w  = Scale(2);
    if (w < 1) w = 1;
    if (x < crc.left || x >= crc.right)  return none;   // 光标不在可视区
    if (y < crc.top  || y >= crc.bottom) return none;
    int bottom = y + lh;
    if (bottom > crc.bottom) bottom = crc.bottom;
    return RECT{ x, y, x + w, bottom };
}

// v1.13.1：HideCaret 是「每调用一次加一」的隐藏计数，ShowCaret 才能减一。
// 若自绘定位失败要退回系统光标，必须把压进去的次数如数还回去，否则用户会
// 彻底看不到插入点。这里自行记账，保证可逆。
static int  g_caretHideN    = 0;      // 已压入的系统光标隐藏计数
static bool g_caretFallback = false;  // 自绘不可用：交还系统光标，不再接管
static int  g_caretMiss     = 0;      // 连续定位失败帧数

void HideSystemCaret(HWND h) {
    if (!h || g_caretFallback) return;
    // v1.13.15：HideCaret 是「每调一次 +1」的计数，而 WM_PAINT 每帧都会走这里，
    // 长时间运行后计数会涨到几万，Detach 时逐个 ShowCaret 会卡住界面。压到
    // 足够深度（8 层，EDIT 自己最多也就 Show 一两次）后就不再累加
    if (g_caretHideN >= 8) return;
    HideCaret(h);
    ++g_caretHideN;
}

static void RestoreSystemCaret(HWND h) {
    if (!h) return;
    while (g_caretHideN > 0) { ShowCaret(h); --g_caretHideN; }
}

// v1.13.2：只失效竖线所在的窄条（旧位置 + 新位置）。此前每一帧都整框
// InvalidateRect，EDIT 会把整段文本重打一遍，闪烁本身就成了持续的小抖动。
static RECT g_caretLast { 0, 0, 0, 0 };

static void CaretInvalidate(HWND h, const RECT& next) {
    if (!h || !IsWindow(h)) return;
    // v1.13.15：g_caretLast 只表示「屏幕上真实存在的那条线」，由 PaintBlueCaret
    // 在画完之后写入。此前这里提前把它改成「将要画的位置」，一旦两帧之间没有
    // WM_PAINT，屏幕上那条更早的线就再也没人失效 —— 于是列表里留下一条永不
    // 消失的蓝线（这正是「光标跑到最前面」的另一半原因）
    if (g_caretLast.bottom > g_caretLast.top) {
        RECT a = g_caretLast;
        InflateRect(&a, Scale(3), Scale(2));
        InvalidateRect(h, &a, FALSE);
    }
    if (next.bottom > next.top) {
        RECT b = next;
        InflateRect(&b, Scale(3), Scale(2));
        InvalidateRect(h, &b, FALSE);
    }
}

// 在当前闪烁相位下把蓝色竖线画到编辑框表面（由子类过程的 WM_PAINT 末尾调用，
// 此时默认过程已画完文字，线条不会被覆盖）
void PaintBlueCaret(HWND h) {
    if (!g_caretOn || g_caretHwnd != h) return;
    // v1.13.15：输入法组词期间不画自绘竖线 —— 此时 EDIT 正在绘制组合串
    //（带下划线），插入点由系统接管，自绘线补进去只会打架
    if (h == g_input && g_imeComposing) return;
    // v1.13.18：行内编辑框组词期间同理 —— 此时 EDIT 正在绘制带下划线的组合串
    if (h == g_rowEdit && g_rowImeComposing) return;
    RECT r = BlueCaretRect(h);
    if (r.right <= r.left || r.bottom <= r.top) {
        // 定位不出来（极端字号 / 光标滚出可视区）：连续 3 帧就退回系统光标，
        // 宁可显示一根默认黑线，也不能让用户完全看不到插入点
        if (!g_caretFallback && ++g_caretMiss >= 3) {
            g_caretFallback = true;
            RestoreSystemCaret(h);
        }
        return;
    }
    g_caretMiss = 0;
    HDC hdc = GetDC(h);
    if (hdc) {
        FillRect(hdc, &r, CachedBrush(COL_ACCENT));
        ReleaseDC(h, hdc);
    }
    g_caretLast = r;      // v1.13.15：记录「已经画在屏幕上的位置」，下一帧据此擦除
}

void AttachBlueCaret(HWND h) {
    if (!h) return;
    if (g_caretHwnd && g_caretHwnd != h) DetachBlueCaret();
    g_caretHwnd     = h;
    g_caretOn       = true;
    g_caretFallback = false;
    g_caretMiss     = 0;
    g_caretHideN    = 0;
    HideSystemCaret(h);                  // 系统黑光标不再出现
    g_caretLast = RECT{ 0, 0, 0, 0 };
    if (g_caretTimer) KillTimer(h, 3);
    g_caretTimer = SetTimer(h, 3, 530, nullptr);
    InvalidateRect(h, nullptr, TRUE);    // 首次接管：整框一次，建立干净底
}

void DetachBlueCaret() {
    HWND h = g_caretHwnd;
    g_caretHwnd = nullptr;
    if (h && IsWindow(h)) {
        if (g_caretTimer) KillTimer(h, 3);
        RestoreSystemCaret(h);              // 交还系统光标
        g_caretFallback = false;
        CaretInvalidate(h, RECT{ 0, 0, 0, 0 });   // 只擦竖线所在窄条
        g_caretLast = RECT{ 0, 0, 0, 0 };
    }
    g_caretTimer = 0;
}

// 编辑框内容 / 光标位置变化后重置相位为「可见」，并整框重绘（旧线随之清除）
void CaretKick(HWND h) {
    if (!h || g_caretHwnd != h || !IsWindow(h)) return;
    g_caretOn = true;
    HideSystemCaret(h);
    if (g_caretTimer) KillTimer(h, 3);
    g_caretTimer = SetTimer(h, 3, 530, nullptr);
    CaretInvalidate(h, BlueCaretRect(h));   // 插入点可能变了：新旧窄条一起失效
}

// 闪烁定时器：翻转相位并整框重绘（WM_PAINT 里按相位决定是否落笔）
void CaretBlink(HWND h) {
    if (!h || g_caretHwnd != h) return;
    g_caretOn = !g_caretOn;
    HideSystemCaret(h);
    CaretInvalidate(h, BlueCaretRect(h));
}

// v1.13.3：让编辑中的插入点留在列表视口内 —— 已展开的长任务行高可能超过
// 视口，光标移到视口外时列表跟着滚（只改滚动位置、不动行高；编辑框由
// RepositionRowEditor 跟随所在行一起位移）
static void EnsureCaretInList() {
    if (!g_rowEdit || g_rowEditRow < 0 || g_rowEditRow >= (int)g_rows.size()) return;
    RECT cr = BlueCaretRect(g_rowEdit);          // 编辑框客户区坐标
    if (cr.bottom <= cr.top) return;
    RECT ir = RowRect(g_rowEditRow);
    int top = ir.top + Scale(10) + (int)cr.top;  // 编辑框顶 = 行顶 + Scale(10)
    int bot = ir.top + Scale(10) + (int)cr.bottom;
    int margin = Scale(12);
    if (top < margin)
        ListScrollTo(g_scrollPx + top - margin, false);
    else if (bot > g_viewH - margin)
        ListScrollTo(g_scrollPx + bot - (g_viewH - margin), false);
}

// v0.17：行内编辑框矩形（OpenRowEditor 与编辑中尺寸同步共用一份几何）：
//   · 单行内容：紧凑高度（≥ 行高 + 余量），在行内垂直居中（旧外观不变）
//   · 多行内容：与显示态文本区对齐（上下内边距 Scale(10)，占满行内容区）
// v0.18：编辑框高度总是略大于内容所需 —— EDIT 控件在光标落到末行时会
//   检查光标是否「完全可见」，高度恰好等于 N × 行高时光标底部可能越界
//   1~2px，触发整个内容向上滚动一行、第一行被顶出；多行余量由行高公式
//   （TaskRowHeight 编辑态 +Scale(4)）提供，单行紧凑框在这里同样预留，
//   大字号任务（行高超过 Scale(22)）也不再内滚
static RECT RowEditBoxRect(int row, int lines) {
    if (row < 0 || row >= (int)g_rows.size() || g_rows[row] < 0)
        return RECT{ 0, 0, 0, 0 };
    Task& t = g_tasks[(size_t)g_rows[row]];
    int whenW = WhenLabelWidth(t);        // 文本区右端避开日期标签
    RECT ir = RowRect(row);
    int left  = ir.left + Scale(12) + Scale(18) + Scale(12);
    int right = ir.right - Scale(12) - RowGutter() - (whenW ? whenW + Scale(12) : 0);
    if (right - left < Scale(80)) right = left + Scale(80);
    HDC hdc = GetDC(g_list);
    HGDIOBJ of = SelectObject(hdc, t.done ? g_fontDone : g_font);
    int lh = TaskLineHeight(hdc);
    SelectObject(hdc, of);
    ReleaseDC(g_list, hdc);
    if (lines <= 1) {
        // v0.27.1：编辑框窗口顶边 = 显示态单行文本的行矩形顶边（与
        // DrawTaskRow 单行分支同公式：行顶 + (行高-lineH)/2），3px 光标
        // 余量全部留在底部 —— 两态字形同机制同矩形，双击进入编辑零跳动
        //（v0.18 的“末行光标可见”余量保留，仅不再居中分配）
        int bh = lh + Scale(3);
        if (bh < Scale(22)) bh = Scale(22);
        int ty = ir.top + ((ir.bottom - ir.top) - lh) / 2;
        if (ty < ir.top) ty = ir.top;
        if (ty + bh > ir.bottom) bh = ir.bottom - ty;   // 极大字号不溢出行
        if (bh < lh) bh = lh;
        return RECT{ left, ty, right, ty + bh };
    }
    int eh  = (ir.bottom - ir.top) - Scale(20);   // 行高已含 v0.18 的 4px 余量
    if (eh < lh + Scale(3)) eh = lh + Scale(3);
    int top = ir.top + Scale(10);
    return RECT{ left, top, right, top + eh };
}

// v0.18：内容整放得下时，垂直滚动只可能是 EDIT 把末行光标误判为
//「不可见」时的自作主张 —— 首可见行一旦偏离 0 立即滚回顶部，保证
// 第一行永不被顶出视野。
// v0.23：内容超过视口容量（RowEditMaxLines 封顶）时内滚是正当行为，
// 不再钉回 —— 滚动条与滚轮都交给 EDIT 自己管理
static void KeepRowEditFullyVisible(HWND ed) {
    if (!ed) return;
    if (RowEditMaxLines(g_rowEditLines) > 0) return;   // v0.23：封顶模式内滚合法
    int first = (int)SendMessageW(ed, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (first > 0) SendMessageW(ed, EM_LINESCROLL, 0, -first);
}

// v0.23：按内容行数同步编辑框滚动条 —— 内容超高封顶时显示 WS_VSCROLL，
// 放得下时隐藏（保持无滚动条的紧凑外观）。滚动条占宽会改变折行结果，
// 调用方需在样式变化后重新实测行数
static void SyncRowEditScroll(HWND ed, int lines) {
    if (!ed) return;
    bool need = RowEditMaxLines(lines) > 0;
    bool has  = (GetWindowLongPtrW(ed, GWL_STYLE) & WS_VSCROLL) != 0;
    if (need == has) return;
    LONG_PTR st = GetWindowLongPtrW(ed, GWL_STYLE);
    SetWindowLongPtrW(ed, GWL_STYLE,
                      need ? (st | WS_VSCROLL) : (st & ~WS_VSCROLL));
    SetWindowPos(ed, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);
}

// v0.17：把行内编辑框移动 / 缩放到其所在行的当前位置（滚动帧与行高
// 变化后调用，编辑框始终与所在行重合）
// v1.13.3：列表滚动后若编辑框已完全滚出视口，立即保存并关闭 —— 滚轮现在
// 不再强制退出编辑，焦点不能留在一个看不见的控件上
void KeepRowEditorInView() {
    if (!g_rowEdit || g_rowEditRow < 0) return;
    RECT ir = RowRect(g_rowEditRow);
    if (ir.bottom <= 0 || ir.top >= g_viewH) CloseRowEditor(true);
}

void RepositionRowEditor() {
    if (!g_rowEdit || g_rowEditRow < 0) return;
    // v1.13.18：组词期间只摆位置、不动格式矩形与内滚位置。
    // MoveWindow 会让 EDIT 重排，EM_SETRECT / EM_LINESCROLL 更是直接干预排版，
    // 三者都可能把正在提交的组合串的插入点顶回开头
    bool composing = g_rowImeComposing;
    RECT box = RowEditBoxRect(g_rowEditRow, g_rowEditLines);
    if (box.right <= box.left || box.bottom <= box.top) return;
    MoveWindow(g_rowEdit, box.left, box.top, box.right - box.left,
               box.bottom - box.top, TRUE);
    RECT er {}, er0 {};
    if (!composing && GetClientRect(g_rowEdit, &er) && er.right > 0) {
        SendMessageW(g_rowEdit, EM_GETRECT, 0, (LPARAM)&er0);
        if (er0.left != er.left || er0.top != er.top ||
            er0.right != er.right || er0.bottom != er.bottom)
            SendMessageW(g_rowEdit, EM_SETRECT, 0, (LPARAM)&er);
        // v0.23：格式矩形没变就不重设 —— EM_SETRECT 可能重置内滚位置，
        // 封顶模式下会打断用户正在进行的滚动
    }
    if (!composing)
        KeepRowEditFullyVisible(g_rowEdit);   // v0.18：任何重摆后都回正内滚
}

// v0.17：编辑框内容行数变化（Shift+Enter 换行 / 删除换行 / 粘贴多行 /
// 撤销）时，行高与编辑框高度同步生长或回缩 —— 单行任务也能在行内编辑时换行；
// v0.23：滚动条随封顶状态同步（内容增删跨越视口容量时出现/消失），
// 滚动条占宽会改变折行，行数需重新实测（有界循环防抖）
void SyncRowEditorSize() {
    if (!g_rowEdit || g_rowEditRow < 0) return;
    // v1.13.18：输入法组词期间完全冻结重排。组词串（未上屏的候选文字）会
    // 让 EM_GETLINECOUNT 在「单行↔多行」之间抖动，一旦据此执行下面的
    // MoveWindow + EM_SETRECT，EDIT 立刻重排，插入点被清掉 —— 中文输入
    // 「后天」正常、补「下午」时插入点跑到『后』前面，就是这里被触发的。
    // 上屏后文本变化会补发一次（RowEditProc 的 EN_CHANGE 路径 + 组词结束）
    if (g_rowImeComposing) return;
    bool changed = false;
    for (int pass = 0; pass < 4; ++pass) {
        int lines = (int)SendMessageW(g_rowEdit, EM_GETLINECOUNT, 0, 0);
        if (lines < 1) lines = 1;
        if (lines != g_rowEditLines) { g_rowEditLines = lines; changed = true; }
        bool hasV  = (GetWindowLongPtrW(g_rowEdit, GWL_STYLE) & WS_VSCROLL) != 0;
        bool needV = RowEditMaxLines(lines) > 0;
        if (needV == hasV) break;
        SyncRowEditScroll(g_rowEdit, lines);  // 样式变化 → 折行可能变化 → 重测
        changed = true;
    }
    if (!changed) return;                // 行数与滚动条状态都没变，无需重摆
    int idx0 = (g_rowEditRow >= 0 && g_rowEditRow < (int)g_rows.size())
                   ? g_rows[g_rowEditRow] : -1;
    int anchorY = RowAnchorY(idx0);      // v1.13.1：行顶原地不动
    RebuildRowTops();                    // 行高公式含编辑态覆盖，重算前缀和与填塞行
    ListRepaint();
    RestoreRowAnchor(idx0, anchorY);     // 替代 ListEnsureVisible（不再把行底硬滚进视口）
    RepositionRowEditor();
    if (RowEditMaxLines(g_rowEditLines) == 0)
        KeepRowEditFullyVisible(g_rowEdit);  // v0.18：放得下时回正内滚；v0.23 封顶时内滚合法
}

// v1.13.22：行内编辑框的「插入点守恒」，与底部添加框 PassKeepCaret 同规则。
// 只用于真正的文档插入（WM_CHAR / WM_PASTE）：基准取「处理本次消息前的选区
// 起点」，按新增字数推算插入点。IME 消息一律不进这个函数 —— 组词期间插入点
// 完全属于输入法，外部任何 EM_SETSEL 干预都会把光标拽回去。
static LRESULT RowPassKeepCaret(HWND h, UINT m, WPARAM w, LPARAM l) {
    DWORD b0 = 0, b1 = 0;
    CallWindowProcW(g_oldRowEditProc, h, EM_GETSEL, (WPARAM)&b0, (LPARAM)&b1);
    DWORD base = b0;
    int lenBefore = (int)CallWindowProcW(g_oldRowEditProc, h, WM_GETTEXTLENGTH, 0, 0);

    LRESULT r = CallWindowProcW(g_oldRowEditProc, h, m, w, l);

    int lenAfter = (int)CallWindowProcW(g_oldRowEditProc, h, WM_GETTEXTLENGTH, 0, 0);
    int added = lenAfter - lenBefore;
    if (added > 0) {
        DWORD want = base + (DWORD)added;
        if ((int)want > lenAfter) want = (DWORD)lenAfter;
        DWORD a0 = 0, a1 = 0;
        CallWindowProcW(g_oldRowEditProc, h, EM_GETSEL, (WPARAM)&a0, (LPARAM)&a1);
        if (a1 != want)
            CallWindowProcW(g_oldRowEditProc, h, EM_SETSEL, want, want);
    }
    return r;
}

// 行内编辑框过程：Enter 保存、Esc 取消、Tab/失焦保存；
// v0.16：多行编辑框中 Shift+Enter = 插入换行（交给默认过程）
static LRESULT CALLBACK RowEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // v1.13.22：与底部添加框同一套防护 —— 拦截外部（输入法上屏收尾）发来的
    // EM_SETSEL(0,0)，避免刚提交的文字把插入点带回行首。判定条件见 EditProc
    if (m == EM_SETSEL && (int)w == 0 && (int)l == 0) {
        DWORD r0 = 0, r1 = 0;
        CallWindowProcW(g_oldRowEditProc, h, EM_GETSEL, (WPARAM)&r0, (LPARAM)&r1);
        int lenNow = (int)CallWindowProcW(g_oldRowEditProc, h, WM_GETTEXTLENGTH, 0, 0);
        if (r1 > 0 && r1 == (DWORD)lenNow) {
            return 0;      // 拒绝把插入点带回收件箱行首
        }
    }
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ml    = (GetWindowLongPtrW(h, GWL_STYLE) & ES_MULTILINE) != 0;
    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN && !(shift && ml)) { CloseRowEditor(true);  return 0; }
        if (w == VK_ESCAPE) { CloseRowEditor(false); return 0; }
        if (w == VK_TAB)    { CloseRowEditor(true);  return 0; }
    } else if (m == WM_CHAR && (w == L'\r' || w == L'\n')) {
        if (!(shift && ml)) return 0;
    } else if (m == WM_KILLFOCUS) {
        DetachBlueCaret();                 // v1.13.1：先交还光标，再关编辑框
        CloseRowEditor(true);
        return 0;
    } else if (m == WM_TIMER && w == 3) {  // v1.13.1：蓝色光标闪烁
        CaretBlink(h);
        return 0;
    } else if (m == WM_MOUSEWHEEL) {
        // v1.13.4：滚轮语义全应用统一 —— 折叠、展开、编辑，滚轮永远只滚列表。
        // 编辑框内的长文本用右侧滚动条，或用光标移动（方向键 / PgUp / Ctrl+End）
        // 让 EDIT 自己跟随；不再出现「指针在框内滚内容、在框外滚列表」两套手感
        return SendMessageW(g_list, WM_MOUSEWHEEL, w, l);
    }
    // v1.13.18：记录组词起止。START 时记下插入点作为提交后的基准；
    // END 只清标志并补一次重排 —— 此刻组合串刚提交、选区还在收尾，
    // 任何 MoveWindow / EM_SETRECT / EM_LINESCROLL 都会把插入点顶回开头
    if (m == WM_IME_STARTCOMPOSITION) {
        g_rowImeComposing = true;
    } else if (m == WM_IME_ENDCOMPOSITION) {
        g_rowImeComposing = false;
    }
    // v1.13.22：只对真正的文档插入做守恒，IME 消息交给原生 EDIT/输入法
    LRESULT r;
    if (m == WM_CHAR || m == WM_PASTE)
        r = RowPassKeepCaret(h, m, w, l);
    else
        r = CallWindowProcW(g_oldRowEditProc, h, m, w, l);
    // v1.13.18：组词刚上屏时同样不重排 —— 与底部添加框 v1.13.16 的处理一致。
    // 上屏后的文本变化会在下面这条 EN_CHANGE 投递路径里补算
    if (m == WM_IME_ENDCOMPOSITION) {
        PostMessageW(g_list, WM_ROWEDITCHK, 0, 0);
    }
    if (m == WM_PAINT) {                 // v1.13.1：EDIT 每帧都会重新 ShowCaret
        HideSystemCaret(h);              //  —— 逐帧压住系统黑光标，改画蓝线
        PaintBlueCaret(h);
    } else if (m == WM_SETFOCUS) {
        g_rowImeComposing = false;       // v1.13.18：输入法被强制取消时不会发 ENDCOMPOSITION
        AttachBlueCaret(h);
    }
    // v0.17：按键 / 粘贴 / 撤销后内容行数可能变化 —— 行高与编辑框同步；
    // v0.18：编辑框右键菜单的粘贴 / 撤销（WM_PASTE / WM_UNDO / WM_CLEAR）
    // 同样改变内容行数，一并同步
    if (m == WM_KEYDOWN || m == WM_CHAR ||
        m == WM_PASTE || m == WM_UNDO || m == WM_CLEAR)
        SyncRowEditorSize();              // v1.13.18：组词期间函数内部直接返回
    // v0.18：光标 / 选择变化（含单击、拖选、方向键）后 EDIT 可能把末行
    // 光标误判为不可见而内滚一行（第一行被顶出）—— 内容总是放得下时
    // 立即滚回顶部，编辑期间所有文本保持完整可见
    // v1.13.18：组词期间跳过 —— EM_LINESCROLL 属于对 EDIT 的排版干预
    if ((m == WM_KEYDOWN || m == WM_CHAR || m == WM_PASTE || m == WM_UNDO ||
         m == WM_CLEAR || m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK ||
         m == WM_MOUSEMOVE) && !g_rowImeComposing)
        KeepRowEditFullyVisible(h);
    // v1.13.1：插入点/内容变了 —— 蓝线立即回到可见相位并重绘
    if (m == WM_KEYDOWN || m == WM_CHAR || m == WM_PASTE || m == WM_UNDO ||
        m == WM_CLEAR || m == WM_SETTEXT || m == WM_LBUTTONDOWN ||
        m == WM_LBUTTONUP || m == WM_LBUTTONDBLCLK)
        CaretKick(h);
    if (m == WM_MOUSEMOVE && (GetKeyState(VK_LBUTTON) & 0x8000))
        CaretKick(h);                    // 拖选：线跟随新的插入点
    // v1.13.3：插入点跑出视口时列表跟着滚（长任务展开后行高 > 视口）
    // v1.13.18：组词期间跳过 —— 列表滚动会触发 RepositionRowEditor 的
    // MoveWindow，正在提交的组合串会被重排打断
    if ((m == WM_KEYDOWN || m == WM_CHAR || m == WM_LBUTTONDOWN ||
         m == WM_LBUTTONUP) && !g_rowImeComposing)
        EnsureCaretInList();
    return r;
}

// 在任务行上打开行内编辑框（clickPt 为列表客户区坐标，用于把光标放到点击处）
void OpenRowEditor(int row, POINT clickPt) {
    if (row < 0 || row >= (int)g_rows.size() || g_rows[row] < 0) return;
    int idx0 = g_rows[row];
    CloseRowEditor(true);                    // 已有编辑框先保存
    int row2 = RowOfIdx(idx0);               // v1.13.1：保存可能重建行，重定位
    if (row2 < 0) return;
    row = row2;
    int idx = idx0;
    RECT ir = RowRect(row);                  // v1.13.0：自绘列表行矩形
    if (ir.bottom <= ir.top) return;
    // v1.13.5：锁定行顶的视口位置 —— 双击触发展开后，用户点的那一行不移动
    int anchorY = RowAnchorY(idx);
    // 文本区右端避开日期标签（与 DrawTaskRow 布局一致，含「＋ 日期」占位）
    Task& t = g_tasks[(size_t)idx];
    int whenW = WhenLabelWidth(t);
    int left  = ir.left + Scale(12) + Scale(18) + Scale(12);
    int right = ir.right - Scale(12) - RowGutter() - (whenW ? whenW + Scale(12) : 0);
    if (right - left < Scale(80)) right = left + Scale(80);
    // v1.12.1：是否多行按折行结果判定（无 \n 的长文本折行后同样用多行编辑框）；
    // 折行超过 5 行且当前折叠时，先展开任务再取行矩形（编辑框盖住全部行）
    HDC hdc = GetDC(g_list);
    HGDIOBJ of = SelectObject(hdc, t.done ? g_fontDone : g_font);
    std::vector<std::wstring> lines = WrapTaskText(hdc, t.text, right - left);
    SelectObject(hdc, of);
    ReleaseDC(g_list, hdc);
    int total = (int)lines.size();

    // v1.13.5：双击即展开到全文高度 —— 编辑态与展开态共用同一条行高公式，
    // 编辑框只是覆盖在整段文字上的一层，进出编辑行高逐像素不变。
    // 退出编辑（Esc / 点其他任务 / 点空白）只销毁编辑框：保持展开、
    // 继续显示「收起」按钮，即「展开选中态 + 一个光标」
    if (!t.expanded) {
        t.expanded = true;
        RefreshListKeepSel();
        row = RowOfIdx(idx);
        if (row < 0) return;
        ir = RowRect(row);
    }
    // v0.17：编辑框一律按多行创建（ES_MULTILINE + ES_AUTOVSCROLL +
    // ES_WANTRETURN）—— 单行任务双击编辑后同样能 Shift+Enter 换行，行高随
    // 内容行数生长（SyncRowEditorSize）；单行内容时保持紧凑居中的旧外观。
    // v0.25：去掉 WS_BORDER —— 灰色边框线条不再显示，编辑框与选中行底色融合
    DWORD edStyle = WS_CHILD | WS_VISIBLE | ES_LEFT |
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    g_rowEditRow   = row;
    g_rowEditIdx   = idx;
    g_rowEditLines = (total > 1) ? total : 1;
    // v0.23：内容超过视口容量时编辑框封顶并带滚动条（放得下保持无滚动条外观）
    if (RowEditMaxLines(g_rowEditLines) > 0) edStyle |= WS_VSCROLL;
    RECT box = RowEditBoxRect(row, g_rowEditLines);
    if (box.right <= box.left || box.bottom <= box.top) {
        g_rowEditRow   = -1;
        g_rowEditIdx   = -1;
        g_rowEditLines = 0;
        return;
    }
    // v0.22：空控件创建、设好字体与格式矩形后由 WM_SETTEXT 载入文本
    //（EDIT 只认 \r\n 为换行，纯 \n 会挤成一行）。此前 CreateWindow 直接
    // 载入文本再设字体/改格式矩形，控件的折行是懒重算的，随后的
    // EM_GETLINECOUNT 可能拿到按旧字体折行的过期结果
    g_rowEdit = CreateWindowExW(0, L"EDIT", nullptr, edStyle,
        box.left, box.top, box.right - box.left, box.bottom - box.top,
        g_list, (HMENU)(INT_PTR)IDC_ROWEDIT, GetModuleHandleW(nullptr), nullptr);
    if (!g_rowEdit) {
        g_rowEditRow   = -1;
        g_rowEditIdx   = -1;
        g_rowEditLines = 0;
        return;
    }
    SendMessageW(g_rowEdit, WM_SETFONT,
                 (WPARAM)(t.done ? g_fontDone : g_font), TRUE);
    // 清除 EDIT 默认内边距，编辑框内容区与显示态文本区完全对齐（不内滚）。
    // v0.27.1：单行编辑框窗口顶边已与显示态行矩形顶边同位（见
    // RowEditBoxRect 单行分支与 DrawTaskRow 单行分支：同一 ty 公式、
    // 同一顶对齐机制），格式矩形取满客户区即可两态字形同位，无需再做
    // 居中补偿；多行框同样满客户区（行顶对齐，与显示态多行绘制同位）
    RECT er {};
    GetClientRect(g_rowEdit, &er);
    SendMessageW(g_rowEdit, EM_SETRECT, 0, (LPARAM)&er);
    SendMessageW(g_rowEdit, WM_SETTEXT, 0, (LPARAM)ToCrLf(t.text).c_str());
    // v0.22：首次打开一律同步重摆 —— 行高按编辑态公式重算（含 4px 余量）
    // 并重摆编辑框。此前实测行数恰好等于预判时直接跳过，而首次打开算
    // 矩形时编辑框还不存在（TaskRowHeight 走显示态公式、无余量），编辑框
    // 高度恰好等于 行数×行高，末行光标底部越界 1~2px 被裁（又被「首可见
    // 行钉回顶部」的保护拉回），光标点到最后一行最后位置始终看不见；
    // 按键后 SyncRowEditorSize 才用编辑态公式补上余量（第二次编辑才正常）
    int realLines = (int)SendMessageW(g_rowEdit, EM_GETLINECOUNT, 0, 0);
    if (realLines > 0 && realLines != g_rowEditLines)
        g_rowEditLines = realLines;   // 控件实测优先（折行差 1 行场景，v0.20）
    SyncRowEditScroll(g_rowEdit, g_rowEditLines);   // v0.23：按实测行数复核滚动条
    RebuildRowTops();
    ListRepaint();
    RestoreRowAnchor(idx, anchorY);   // v1.13.5：行顶原地不动（展开后仍停在原处）
    RepositionRowEditor();
    KeepRowEditFullyVisible(g_rowEdit);
    g_oldRowEditProc = (WNDPROC)SetWindowLongPtrW(g_rowEdit, GWLP_WNDPROC,
                                                  (LONG_PTR)RowEditProc);
    // v0.20：先给焦点再置光标 —— 有焦点时 EM_SETSEL 才会创建并正确定位
    // caret；无焦点时置末尾、SetFocus 后多行场景光标可能不显示
    SetFocus(g_rowEdit);
    int len = GetWindowTextLengthW(g_rowEdit);
    // v1.13.8：光标落到「双击那一行的行尾」，而不是行首、也不是点击处 ——
    // 任务文本是逐行写的一条条内容，双击的意图基本都是在这一行后面续写；
    // 落在行首会把新字符插到行号/开头文字之前，落在行中间则要先把光标挪到
    // 末尾才能继续打。这里先定位到点击所在的行，再取该行末尾
    int caret = len;
    if (clickPt.x >= 0 && clickPt.y >= 0) {
        POINT cp = { clickPt.x - box.left, clickPt.y - box.top };
        // 点击常落在行的上下内边距（文本矩形之外），EM_CHARFROMPOS 会返回 -1；
        // 先钳进客户区，点行顶留白算首行、点行底留白算末行
        RECT ec {};
        GetClientRect(g_rowEdit, &ec);
        if (cp.x < ec.left) cp.x = ec.left;
        if (cp.y < ec.top)  cp.y = ec.top;
        if (cp.x > ec.right  - 1) cp.x = ec.right  - 1;
        if (cp.y > ec.bottom - 1) cp.y = ec.bottom - 1;

        int line = -1;
        LRESULT ci = SendMessageW(g_rowEdit, EM_CHARFROMPOS, 0, (LPARAM)&cp);
        int ciIdx = (int)(short)LOWORD(ci);
        if (ci >= 0 && ciIdx >= 0 && ciIdx <= len)
            line = (int)SendMessageW(g_rowEdit, EM_LINEFROMCHAR, (WPARAM)ciIdx, 0);
        if (line < 0) {
            // 兜底：按编辑框行高反推行号
            TEXTMETRICW tm2 {};
            HDC hdc2 = GetDC(g_rowEdit);
            HFONT f2 = (HFONT)SendMessageW(g_rowEdit, WM_GETFONT, 0, 0);
            HGDIOBJ of2 = f2 ? SelectObject(hdc2, f2) : nullptr;
            GetTextMetricsW(hdc2, &tm2);
            if (of2) SelectObject(hdc2, of2);
            ReleaseDC(g_rowEdit, hdc2);
            int lh2 = (tm2.tmHeight > 0)
                          ? (int)tm2.tmHeight + (int)tm2.tmExternalLeading
                          : Scale(16);
            line = (lh2 > 0 && cp.y > 0) ? (cp.y / lh2) : 0;
        }
        if (line >= 0) {
            LRESULT ls = SendMessageW(g_rowEdit, EM_LINEINDEX, (WPARAM)line, 0);
            if ((int)ls >= 0 && (int)ls <= len) {
                int lineLen = (int)SendMessageW(g_rowEdit, EM_LINELENGTH,
                                                 (WPARAM)(int)ls, 0);
                if (lineLen < 0) lineLen = 0;
                caret = (int)ls + lineLen;      // 该行行尾
                if (caret > len) caret = len;
            }
        }
    }
    SendMessageW(g_rowEdit, EM_SETSEL, caret, caret);
    // v0.23：封顶模式下框只显示部分内容，滚到光标处（内容末端）让光标
    // 可见；未封顶时内容整篇铺开，保持 v0.22「首行在顶」的行为
    if (RowEditMaxLines(g_rowEditLines) > 0)
        SendMessageW(g_rowEdit, EM_SCROLLCARET, 0, 0);
    // v0.18/v0.22：光标落到末行时 EDIT 可能误滚动一行（第一行被顶出），
    // 立即回正；置选/焦点可能触发控件重算折行 —— 再复核一次行数
    int afterLines = (int)SendMessageW(g_rowEdit, EM_GETLINECOUNT, 0, 0);
    if (afterLines > 0 && afterLines != g_rowEditLines) SyncRowEditorSize();
    else KeepRowEditFullyVisible(g_rowEdit);
    // v0.23：封顶模式下重摆/复核可能重置内滚位置 —— 最后再滚到光标一次
    if (RowEditMaxLines(g_rowEditLines) > 0)
        SendMessageW(g_rowEdit, EM_SCROLLCARET, 0, 0);
    // v1.13.3：编辑框可能比视口高（已展开的长任务），把光标滚进视口
    EnsureCaretInList();
    // v0.22：首帧绘制后再异步复核一次行数（列表过程收 WM_ROWEDITCHK），
    // 兜底任何残存的懒折行时序 —— 任何路径下末行光标都完整可见
    PostMessageW(g_list, WM_ROWEDITCHK, 0, 0);
}
