// 输入框（子类化 / 占位 / 居中）
#include "common.h"
#include "ui/input_box.h"
#include "app/actions.h"
#include "ui/gdi_util.h"
#include "app/ids.h"
#include "ui/list_view.h"
#include "core/date_parse.h"
#include "ui/row_editor.h"
#include "ui/context.h"
#include "ui/theme.h"
#include "core/time_util.h"
#include "ui/main_window.h"


// v2.1：输入框格式矩形（EM_SETRECT）的缓存。
// 原因：WM_SETFONT 会让 EDIT 把格式矩形丢掉、变回整块客户区（实测：
// EM_SETRECT 设成 0,10,200,26 后发 WM_SETFONT，EM_GETRECT 读回 3,0,194,27），
// 而缓存还记着「上次已经设成居中」—— CenterInput 于是误判无变化直接返回，
// 单行文字与光标就贴在顶部，不再垂直居中。
// 因此凡是「动过字体 / 可能让 EDIT 丢弃格式矩形」的地方，都要先失效这份缓存。
static RECT s_inFmtRect  {};
static bool s_inFmtValid = false;
void InvalidateInputFmtRect() { s_inFmtValid = false; }

RECT   g_hintRect   {};
std::wstring g_hintText;   // 输入框实时识别提示
RECT   g_inputFrame {};    // v0.27 输入框外描边矩形（Layout 更新，父窗口绘制）

void UpdateInputHint() {
    int n = GetWindowTextLengthW(g_input);
    std::wstring raw;
    raw.resize((size_t)n + 1);
    GetWindowTextW(g_input, &raw[0], n + 1);
    raw.resize(wcslen(raw.c_str()));
    if (raw.find(L'\n') != std::wstring::npos) {
        // v1.12.0 多行粘贴：提示整段添加
        int lines = (int)SendMessageW(g_input, EM_GETLINECOUNT, 0, 0);
        if (lines < 1) lines = 1;
        g_hintText = L"多行文本 " + std::to_wstring(lines) +
                     L" 行 · 回车整段添加，Shift+Enter 换行";
    } else {
        WhenResult w = ParseWhen(raw);
        if (w.ok) {
            std::wstring when = FormatWhen(w.due, w.hasTime, false, nullptr);
            if (!when.empty())
                g_hintText = L"识别到：" + when + L"（回车添加，自动归类到任务）";
            else
                g_hintText.clear();
        } else {
            g_hintText.clear();
        }
    }
    InvalidateRect(g_hwnd, &g_hintRect, TRUE);
}

// v1.12.0：输入框随内容行数长高（粘贴多行文本时），并随 Layout 重新排布
int g_inputH = 0;   // 0 = 尚未按内容计算（Layout 按 1 行高度处理）
void UpdateInputSize() {
    if (!g_input || !g_hwnd) return;
    // v1.13.17：输入法组词期间不做任何布局 —— 组词串会让 EM_GETLINECOUNT 抖动
    // （单行 ↔ 双行），一旦据此走 Layout，就会 MoveWindow 改输入框尺寸，
    // 编辑框重排、插入点被清掉。上屏后文本变化会再发 EN_CHANGE，届时补算
    if (g_imeComposing) return;
    int lines = (int)SendMessageW(g_input, EM_GETLINECOUNT, 0, 0);
    if (lines < 1) lines = 1;
    int h = (lines <= 1) ? Scale(36)
          : Scale(24) + ((lines > 4) ? 4 : lines) * Scale(22);
    if (h != g_inputH) {
        g_inputH = h;
        Layout();                        // Layout 内部已调 CenterInput
        InvalidateRect(g_hwnd, nullptr, TRUE);
    }
    // v1.13.16：行数没变就什么都不做 —— 格式矩形只取决于窗口尺寸（Layout 负责）
    // 与字体（ApplyFonts 负责），这里没有需要重算的东西。此前无条件调
    // CenterInput，让「每敲一个字都去动一次 EDIT 的格式矩形」，是输入时插入点
    // 被打断的总根源
}

// ---------------------------- 输入框光标垂直居中 ----------------------------
void CenterInput(bool force) {
    if (!g_input) return;
    // v1.13.14：输入法组词期间绝不动格式矩形 —— EM_SETRECT 会让 EDIT 重排，
    // 把组合串的插入点甩回开头（输入「后天下午」时最明显）
    if (g_imeComposing) return;
    // 每次从完整客户区出发重算（EM_GETRECT 会返回上次修改过的矩形，不可作基准）
    RECT rc {};
    GetClientRect(g_input, &rc);
    HDC hdc = GetDC(g_input);
    HGDIOBJ of = SelectObject(hdc, g_font);
    TEXTMETRICW tm {};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, of);
    ReleaseDC(g_input, hdc);
    int boxH = rc.bottom - rc.top;
    // v1.12.0：多行内容从顶部排布（仅留小边距）；单行保持垂直居中
    int lines = (int)SendMessageW(g_input, EM_GETLINECOUNT, 0, 0);
    int off = (lines > 1) ? Scale(5) : (boxH - tm.tmHeight) / 2;
    if (off < 0) off = 0;
    RECT rc2 = rc;
    rc2.top += off;
    rc2.bottom -= off;
    rc2.left  += Scale(8);   // v0.27：去 CLIENTEDGE 后文字与描边保持间距
    rc2.right -= Scale(8);   //（与 EM_SETMARGINS 同值，占位符 EM_GETRECT 同源）
    if (rc2.bottom < rc2.top + tm.tmHeight) rc2.bottom = rc2.top + tm.tmHeight;

    // v1.13.14：格式矩形没变就不要重设 —— 每次键入都会走 EN_CHANGE ->
    // UpdateInputSize -> CenterInput，而 EM_SETRECT 会让 EDIT 重算排版，
    // 输入「后天下午」这类会被实时识别的词时，光标会被甩回文本开头
    // v2.1：force = true 时无视缓存强制重设（换字体 / 切主题后补救）
    if (!force && s_inFmtValid && EqualRect(&rc2, &s_inFmtRect)) return;
    s_inFmtRect  = rc2;
    s_inFmtValid = true;

    // v1.13.17：删掉「先存选区、SETRECT 后写回」这段 —— 写回的是读取瞬间的值，
    // 如果那一刻正处在输入法提交过程中，读到的是组合串起点，写回去等于把插入
    // 点顶到最前面，正是用户看到的「文字插到『后』前面」。EDIT 内部按字符索引
    // 维护插入点，EM_SETRECT 本身不会让它归零，恢复逻辑只有害处
    SendMessageW(g_input, EM_SETRECT, 0, (LPARAM)&rc2);
}

// ---------------------------- 子类化过程 ------------------------------------
// v0.27：输入框占位符文案 —— 空且未聚焦时在框内绘制（次级色）
static const wchar_t* kInputPlaceholder = L"添加新任务，回车确认…";

// 占位符绘制：先走默认过程完成自身绘制，再叠画在格式矩形内。
// 位置与真实文字同源（EM_GETRECT 返回 CenterInput 设置的格式矩形）
static void DrawInputPlaceholder(HWND h) {
    if (GetWindowTextLengthW(h) != 0 || GetFocus() == h) return;
    RECT fr;
    SendMessageW(h, EM_GETRECT, 0, (LPARAM)&fr);
    HDC dc = GetDC(h);
    HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HGDIOBJ of = SelectObject(dc, f ? (HGDIOBJ)f
                                    : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_MUTED);
    DrawTextW(dc, kInputPlaceholder, -1, &fr,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, of);
    ReleaseDC(h, dc);
}

// 普通字符输入 / 粘贴的插入点兜底。
// 重要：不要把 WM_IME_CHAR / WM_IME_COMPOSITION 交给这个函数。
// IME 可能先发送多条中间组合串消息，再发送最终结果；此时每条消息后的文本
// 长度差并不等于这次组词最终插入的字符数。若在 IME 消息中调用 EM_SETSEL，
// 就可能按旧的组词起点反复设置插入点，使光标跳回文本前面。
// 输入法消息必须由原生 EDIT/IME 自己管理。输入期间的尺寸与格式矩形更新已由
// g_imeComposing、UpdateInputSize、CenterInput 及异步 EN_CHANGE 路径隔离。
static LRESULT PassKeepCaret(HWND h, UINT m, WPARAM w, LPARAM l) {
    DWORD b0 = 0, b1 = 0;
    CallWindowProcW(g_oldEditProc, h, EM_GETSEL, (WPARAM)&b0, (LPARAM)&b1);
    DWORD base = b0;
    int lenBefore = (int)CallWindowProcW(g_oldEditProc, h, WM_GETTEXTLENGTH, 0, 0);

    LRESULT r = CallWindowProcW(g_oldEditProc, h, m, w, l);

    int lenAfter = (int)CallWindowProcW(g_oldEditProc, h, WM_GETTEXTLENGTH, 0, 0);
    int added = lenAfter - lenBefore;
    if (added > 0) {
        DWORD want = base + (DWORD)added;
        if ((int)want > lenAfter) want = (DWORD)lenAfter;
        DWORD a0 = 0, a1 = 0;
        CallWindowProcW(g_oldEditProc, h, EM_GETSEL, (WPARAM)&a0, (LPARAM)&a1);
        if (a1 != want)
            CallWindowProcW(g_oldEditProc, h, EM_SETSEL, want, want);
    }
    return r;
}

LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // v1.13.22：拦截外部发来的 EM_SETSEL(0,0)。
    //
    // 由插入点追踪日志实测确认：输入「后天下午」时四个字全部正确插入，插入点
    // 也已正确停在末尾 (4,4)；紧接着会收到一条 0x00B1（EM_SETSEL）把选区改成
    // (0,0)，而文本长度仍是 4。这条消息不来自本程序任何调用点（PowerCaret 与
    // Layout 的每次 EM_SETSEL 都在追踪范围内且值正确），是输入法在上屏收尾时
    // 发来的「插入点纠正」。它一旦生效，光标就停在开头，下一段组词从 0 位置
    // 开始提交 —— 这正是「光标回到最前面 / 文字插到『后』前面」的成因。
    //
    // 判定条件刻意收得很窄，避免误伤正常操作：
    //   · 只拦 w==0 && l==0（把光标放到开头并取消选择）
    //   · 只在文本非空、且当前插入点正好在文本末尾时拦
    // 用户主动点击 / Home 键 / 拖选都不走这条路径（那些是鼠标或键盘消息，由
    // EDIT 内部直接更新选区）；Layout 的选区恢复若真是 (0,0)，说明插入点本就
    // 在开头，此时 b1 不会是末尾，也不会命中。
    if (m == EM_SETSEL && (int)w == 0 && (int)l == 0) {
        DWORD b0 = 0, b1 = 0;
        CallWindowProcW(g_oldEditProc, h, EM_GETSEL, (WPARAM)&b0, (LPARAM)&b1);
        if (b1 > 0) {
            int lenNow = (int)CallWindowProcW(g_oldEditProc, h,
                                             WM_GETTEXTLENGTH, 0, 0);
            if (b1 == (DWORD)lenNow) return 0;    // 拒绝复位，保住末尾插入点
        }
    }
    if (m == WM_KEYDOWN && w == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        ToggleSearch();                  // v2.3.3：输入任务时也能直接开搜索
        return 0;
    }
    if (m == WM_CHAR && (w == L'\r' || w == L'\n')) {
        // v0.16：Shift+Enter = 在输入框内换行（多行 EDIT 默认过程插入换行），
        // Enter = 添加任务
        if (GetKeyState(VK_SHIFT) & 0x8000)
            return CallWindowProcW(g_oldEditProc, h, m, w, l);
        AddFromInput();
        return 0;
    }
    if (m == WM_TIMER && w == 3) {   // v1.13.1：蓝色光标闪烁
        CaretBlink(h);
        return 0;
    }
    // 输入法组词期间冻结内边距/尺寸重算，但不自行保存/恢复 IME 插入点。
    // 原生 EDIT 与输入法负责组合串及最终结果的插入点；子类不应在这些消息中
    // 用 EM_SETSEL 覆盖它。
    if (m == WM_IME_STARTCOMPOSITION) {
        g_imeComposing = true;
    } else if (m == WM_IME_ENDCOMPOSITION) {
        g_imeComposing = false;
        // 延迟补算（绝不在 END 消息里同步重排）。即使 EN_CHANGE 投递的消息
        // 已在组词标志清除前执行并提前返回，这条消息也会在组词结束后再次补算。
        if (g_hwnd && IsWindow(g_hwnd))
            PostMessageW(g_hwnd, WM_INPUTCHANGED, 0, 0);
    }
    if (m == WM_PAINT) {         // v0.27：空且未聚焦时叠画占位符
        LRESULT r = CallWindowProcW(g_oldEditProc, h, m, w, l);
        DrawInputPlaceholder(h);
        HideSystemCaret(h);      // v1.13.1：压掉系统黑光标，改画蓝线
        PaintBlueCaret(h);
        return r;
    }
    if (m == WM_SETFOCUS) {
        g_imeComposing = false;  // 输入法被强制取消时不会发 ENDCOMPOSITION
        DeselectList();          // 点到输入框等其他位置 -> 取消任务选中
        LRESULT r = CallWindowProcW(g_oldEditProc, h, m, w, l);
        InvalidateRect(h, nullptr, TRUE);             // 占位符隐藏
        InvalidateRect(g_hwnd, &g_inputFrame, FALSE); // 描边变强调色
        AttachBlueCaret(h);                           // v1.13.1：接管光标
        return r;
    }
    if (m == WM_KILLFOCUS) {     // v0.27：焦点离开 -> 占位符/描边重绘
        DetachBlueCaret();                             // v1.13.1：交还光标
        LRESULT r = CallWindowProcW(g_oldEditProc, h, m, w, l);
        InvalidateRect(h, nullptr, TRUE);             // 占位符显示
        InvalidateRect(g_hwnd, &g_inputFrame, FALSE); // 描边回边框色
        return r;
    }
    // 普通字符和粘贴可使用插入点兜底；IME 消息必须由原生 EDIT/输入法处理，
    // 不在组词串更新或提交过程中主动发送 EM_SETSEL。
    LRESULT r;
    if (m == WM_CHAR || m == WM_PASTE)
        r = PassKeepCaret(h, m, w, l);
    else
        r = CallWindowProcW(g_oldEditProc, h, m, w, l);
    // v1.13.1：插入点/内容变了 —— 蓝线回到可见相位并重绘
    if (m == WM_KEYDOWN || m == WM_CHAR || m == WM_PASTE || m == WM_UNDO ||
        m == WM_CLEAR || m == WM_SETTEXT || m == WM_LBUTTONDOWN ||
        m == WM_LBUTTONUP || m == WM_LBUTTONDBLCLK)
        CaretKick(h);
    if (m == WM_MOUSEMOVE && (GetKeyState(VK_LBUTTON) & 0x8000))
        CaretKick(h);
    return r;
}
