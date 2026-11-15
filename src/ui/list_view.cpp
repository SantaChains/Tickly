// 自绘任务列表（几何 / 滚动 / 绘制 / 交互）
#include "common.h"
#include "ui/list_view.h"
#include "app/actions.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "app/ids.h"
#include "ui/row_editor.h"
#include "ui/context.h"
#include "app/store.h"
#include "core/task.h"
#include "core/text_util.h"
#include "ui/theme.h"

#include <unordered_map>
#include "core/time_util.h"
#include "ui/main_window.h"

// v1.13.0：列表改为完全自绘子窗口（像素级平滑滚动），选中/滚动状态自管理
int    g_selRow = -1;          // 选中的显示行（原 ListBox GetCurSel）
// v0.16 多选：g_multiSel 保存选中的任务下标（g_tasks 索引，重建行后仍有效），
// Shift+单击批量选（锚点 g_anchorRow 到单击行）、Ctrl+单击逐个切换、Ctrl+A 全选；
// 多选后右键 / 底部按钮可批量完成 / 还原 / 删除
std::vector<int> g_multiSel;
int g_anchorRow = -1;          // Shift 批量选锚点（最后一次普通单击的显示行）
double g_scrollPx  = 0.0;      // 当前滚动像素（内容顶部被移出视口的高度）
double g_scrollTgt = 0.0;      // 滚轮动画目标像素
UINT_PTR g_scrollTimer = 0;    // 滚动动画定时器（列表窗口内 id 1）
std::vector<int> g_rowTops;    // 每行 top y（内容坐标，size = 行数+1）
int    g_totalH = 0;           // 内容总高（含填塞行）
int    g_viewH  = 0;           // 列表视口高
static bool   g_sbDrag  = false;      // 正在拖动滚动条
static bool   g_sbTrack = false;      // 鼠标悬停在滚动条热区
static int    g_sbGrabOff = 0;        // 拖动抓取点相对 thumb 顶部的偏移
bool   g_sbVis   = false;      // v0.24 thumb 是否可见（滚动/悬停唤出）
UINT_PTR g_sbTimer = 0;        // v0.24 自动隐藏倒计时（列表窗口 id 2）
int    g_fillRow = -1;           // 填塞行在 g_rows 中的下标（-1 = 无）
int    g_hoverRow   = -1;  // 勾选框悬停的行（-1 无）
bool   g_hoverBox   = false; // 悬停是否落在勾选框热区
static int    g_hoverLine  = -1;  // v0.27 整行悬停的行（-1 无，与选中/勾选悬停独立）
static bool   g_trackMouse = false; // 是否已请求 WM_MOUSELEAVE 跟踪

// 勾选框热区：行左缘 ~ 40 DIP（框绘制在 left+12~left+30，热区含前后缓冲），
// 与 DrawTaskRow 的绘制保持同步
static bool PtInBoxZone(const RECT& ir, int x) {
    return x >= ir.left && x < ir.left + Scale(40);
}

// 把一个词放入当前行；放不下时换行；单词本身超宽（如长 URL、无空格的
// 中文长段落）按字符硬断 —— v1.12.1 改二分查找每行最大可容纳前缀，
// 测量次数从逐字 O(N) 降到每行 O(log N)，数千字长文本行高计算不再卡顿
void PushWrappedWord(HDC hdc, const std::wstring& word, int maxW,
                            std::wstring& cur, std::vector<std::wstring>& lines) {
    if (word.empty()) return;
    SIZE sz {};
    std::wstring cand = cur.empty() ? word : cur + L' ' + word;
    GetTextExtentPoint32W(hdc, cand.c_str(), (int)cand.size(), &sz);
    if (sz.cx <= maxW) { cur = cand; return; }
    if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
    if (word.size() == 1) { cur = word; return; }   // 单字符也超宽：只能保留
    size_t pos = 0;
    for (;;) {
        // 在 word[pos..] 上二分找最大 fit：宽度(word[pos..pos+fit)) <= maxW
        size_t lo = 1, hi = word.size() - pos, fit = 1;
        while (lo <= hi) {
            size_t mid = lo + (hi - lo) / 2;
            GetTextExtentPoint32W(hdc, word.c_str() + pos, (int)mid, &sz);
            if (sz.cx <= maxW) { fit = mid; lo = mid + 1; }
            else {
                if (mid == 0) break;
                hi = mid - 1;
            }
        }
        if (pos + fit >= word.size()) {          // 剩余部分整段放得下
            cur.assign(word, pos, std::wstring::npos);
            return;
        }
        lines.emplace_back(word, pos, fit);      // 满行断开，继续下一行
        pos += fit;
    }
}

// 行高/宽度测量用的共享 DC。原实现每行 GetDC/ReleaseDC 各一次（5000 条任务
// 就是 1 万次窗口 DC 往返，实测占启动时间一半以上）；这里换成进程内内存 DC，
// 选中同一套字体后度量结果逐值相同，几何不变。
namespace {
HDC MeasureDC() {
    static const HDC dc = [] {
        HDC d = CreateCompatibleDC(nullptr);
        if (d) {
            HBITMAP b = CreateCompatibleBitmap(d, 8, 8);
            if (b) SelectObject(d, b);
        }
        return d;
    }();
    return dc;
}
}  // namespace

// 任务文本按显示宽度折行：\n 为硬换行；空格为软断点（贪心装行）。
// 结果同时用于行高计算（TaskRowHeight）与绘制（DrawTaskRow），几何一致。
namespace {

struct WrapEntry {
    int                      width  = 0;
    int                      fontPx = 0;
    std::vector<std::wstring> lines;
};

constexpr size_t kWrapCacheMax = 8192;   // 超出直接清空（正常使用远达不到）
std::unordered_map<std::wstring, std::vector<WrapEntry>> g_wrapCache;

std::vector<std::wstring> WrapTaskTextCompute(HDC hdc, const std::wstring& text,
                                             int maxW) {
    std::vector<std::wstring> lines;
    if (maxW < Scale(20)) maxW = Scale(20);
    size_t p = 0;
    for (;;) {
        size_t e = text.find(L'\n', p);
        std::wstring para = text.substr(p,
            (e == std::wstring::npos) ? std::wstring::npos : e - p);
        std::wstring cur;
        size_t q = 0;
        for (;;) {
            size_t r = para.find(L' ', q);
            std::wstring word = para.substr(q,
                (r == std::wstring::npos) ? std::wstring::npos : r - q);
            PushWrappedWord(hdc, word, maxW, cur, lines);
            if (r == std::wstring::npos) break;
            q = r + 1;
        }
        lines.push_back(cur);          // 段落末行（NormalizeMultiline 后不为空行）
        if (e == std::wstring::npos) break;
        p = e + 1;
    }
    return lines;
}
// 取缓存（未命中则计算并存入）；返回的指针只在本次调用内有效
const std::vector<std::wstring>* WrapCached(HDC hdc, const std::wstring& text, int maxW) {
    if (maxW < Scale(20)) maxW = Scale(20);
    const int fp = TaskLineHeight(hdc);
    if (g_wrapCache.size() > kWrapCacheMax) g_wrapCache.clear();
    std::vector<WrapEntry>& bucket = g_wrapCache[text];
    for (const WrapEntry& e : bucket)
        if (e.width == maxW && e.fontPx == fp) return &e.lines;
    WrapEntry entry;
    entry.width  = maxW;
    entry.fontPx = fp;
    entry.lines  = WrapTaskTextCompute(hdc, text, maxW);
    bucket.push_back(std::move(entry));
    return &bucket.back().lines;
}

}  // namespace

std::vector<std::wstring> WrapTaskText(HDC hdc, const std::wstring& text,
                                      int maxW) {
    return *WrapCached(hdc, text, maxW);
}

int WrapTaskLineCount(HDC hdc, const std::wstring& text, int maxW) {
    return (int)WrapCached(hdc, text, maxW)->size();
}

// 日期标签宽度（行高计算与 DrawTaskRow 用同一公式，保证几何一致）
int WhenLabelWidth(const Task& t) {
    std::wstring when = WhenLabelText(t);
    if (when.empty()) return 0;
    HDC hdc = MeasureDC();
    // v0.26：与 DrawTaskRow 同源 —— 未完成任务用可设置的日期字体测量，
    // 已完成保持固定小字号（两字号不同时折行预留宽度也不同）
    HGDIOBJ ow = SelectObject(hdc, t.done ? g_fontSub : g_fontDate);
    SIZE sz {};
    GetTextExtentPoint32W(hdc, when.c_str(), (int)when.size(), &sz);
    SelectObject(hdc, ow);
    return sz.cx;
}

// v1.13.2：多行任务的「行高」必须与 EDIT 控件内部的行距完全一致，否则
// 显示态（自绘 DrawText 逐行排布）与编辑态（EDIT 自己排版）会按不同步长
// 累加 —— GetTextExtentPoint32 只返回 tmHeight（如 16px），而 EDIT 用的是
// tmHeight + tmExternalLeading（如 20px），10 行任务到末行就差 40px，
// 双击瞬间整段文字重新「散开」，这就是抖动的根因。
int TaskLineHeight(HDC hdc) {
    TEXTMETRICW tm {};
    if (GetTextMetricsW(hdc, &tm) && tm.tmHeight > 0)
        return (int)tm.tmHeight + (int)tm.tmExternalLeading;
    SIZE ls {};
    GetTextExtentPoint32W(hdc, L"测", 1, &ls);
    return ls.cy > 0 ? ls.cy : Scale(19);
}

// v0.23：行内编辑框的最大可视行数 —— 任务内容行数超过视口能容纳的行数时，
// 编辑框与所在行封顶为该行数（编辑框出现滚动条、行高不再随内容无限生长）；
// 内容整放得下时返回 0 = 不封顶，保持 v0.18 以来「全内容铺开」的外观。
// 容量 = (视口高 - 行上下边距 24px) / 行高，至少保 4 行（极小窗口也有可编辑
// 区）；字体按当前编辑任务的完成态选择，与 TaskRowHeight 的行高计算同源
int RowEditMaxLines(int contentLines) {
    // v1.13.5：编辑框不再自带滚动条、也不再封顶 —— 双击即展开，编辑框高度
    // 等于展开后的整行高度，内容永远放得下；浏览长文本时滚的是整个列表
    //（与展开态同一套滚轮语义）。恒返回 0，调用点保持原样即可
    (void)contentLines;
    return 0;
}

// 任务行高度：单行任务固定 Scale(40)（与旧版一致）；
// 多行任务 = 上下内边距 + 可见行数 x 行高（未展开 > 5 行时另留「展开」按钮行）
int TaskRowHeight(int idx) {
    if (idx < 0 || idx >= (int)g_tasks.size()) return Scale(40);
    const Task& t = g_tasks[(size_t)idx];
    RECT lrc;
    if (!g_list || !GetClientRect(g_list, &lrc) || lrc.right <= 0) return Scale(40);
    int whenW = WhenLabelWidth(t);
    int textLeft  = Scale(12) + Scale(18) + Scale(12);
    int textRight = lrc.right - Scale(12) - RowGutter() - (whenW ? whenW + Scale(12) : 0);
    HDC hdc = MeasureDC();
    HGDIOBJ of = SelectObject(hdc, t.done ? g_fontDone : g_font);
    int lineH = TaskLineHeight(hdc);
    int total = WrapTaskLineCount(hdc, t.text, textRight - textLeft);
    SelectObject(hdc, of);
    // v1.13.5：编辑态不再单独算高度 —— 双击即展开，编辑框只是盖在「展开态」
    // 文字上的一层，进出编辑行高逐像素相同（用户要求：编辑状态与展开选中
    // 状态高度一致，只多一个光标）。这里只保留控件实测行数优先的兜底
    if (idx == g_rowEditIdx && g_rowEdit != nullptr && g_rowEditLines > total)
        total = g_rowEditLines;   // 折行差 1 行场景（v0.20）
    // v1.12.1：行高按折行结果判定 —— 无 \n 的长文本折行后同样占多行
    if (total <= 1) return Scale(40);
    // v1.13.2：Scale(4) 是末行光标余量，留给编辑框，显示态一并计入
    int btn = (total > kTaskMaxLines) ? Scale(20) : 0;
    int vis = (t.expanded || total <= kTaskMaxLines) ? total : kTaskMaxLines;
    int h = Scale(10) + vis * lineH + Scale(10) + btn + Scale(4);
    return h > Scale(40) ? h : Scale(40);
}

// ---------------------------- 自绘列表行几何（v1.13.0） ----------------------
// 列表已由 ListBox 替换为完全自绘子窗口：行高自己算、滚动自己控（像素级），
// 滚轮带缓动动画、滚动条为悬浮胶囊条且支持像素级拖动，双缓冲绘制无闪烁。
int RowHeightAt(int r) {
    if (r == ROW_FILL) return (int)g_fillH;
    if (r == ROW_DONE_SUMMARY) return Scale(38);   // v1.13.11：汇总行更高一点
    if (r < 0) return Scale(30);
    return TaskRowHeight(r);
}

// 填塞行高度 = 视口高 - 其余行总高（剩余不足则为 0 -> 正常滚动）
UINT CalcFillerHeight() {
    if (!g_list) return 0;
    RECT lrc;
    if (!GetClientRect(g_list, &lrc) || lrc.bottom <= 0) return 0;
    int others = 0;
    for (int r : g_rows) {
        if (r == ROW_FILL) continue;
        others += RowHeightAt(r);
    }
    long long fill = (long long)lrc.bottom - others;
    return fill > 0 ? (UINT)fill : 0;
}

// 重建行 top 前缀和与内容总高；填塞行高度随视口变化在这里统一处理。
// 任何行结构/行高/视口变化后都必须调用（内部处理滚动越界钳制）。
void RebuildRowTops() {
    if (g_fillRow >= 0) g_fillH = CalcFillerHeight();
    size_t n = g_rows.size();
    g_rowTops.assign(n + 1, 0);
    int y = 0;
    for (size_t i = 0; i < n; ++i) {
        g_rowTops[i] = y;
        y += RowHeightAt(g_rows[i]);
    }
    g_rowTops[n] = y;
    g_totalH = y;
    RECT lrc;
    g_viewH = (g_list && GetClientRect(g_list, &lrc)) ? lrc.bottom : 0;
    double maxPx = (g_totalH > g_viewH) ? (double)(g_totalH - g_viewH) : 0.0;
    if (g_scrollPx > maxPx) g_scrollPx = maxPx;
    if (g_scrollTgt > maxPx) g_scrollTgt = maxPx;
}

int RowTop(int r) {
    return (r >= 0 && r < (int)g_rowTops.size()) ? g_rowTops[r] : 0;
}

// 视口坐标 -> 显示行（含填塞行；y 超出内容底部时返回最后一行）
static int RowFromY(int viewY) {
    if (g_rowTops.empty() || g_rows.empty()) return -1;
    int y = viewY + (int)g_scrollPx;
    int n = (int)g_rows.size();
    if (y < 0) y = 0;
    if (y >= g_totalH) y = g_totalH - 1;
    int lo = 0, hi = n - 1;   // g_rowTops 单调不减，二分找最后一个 top<=y 的行
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (g_rowTops[mid] <= y) lo = mid; else hi = mid - 1;
    }
    return lo;
}

// 显示行 -> 视口坐标矩形（整行，可能与视口交叠出界）
RECT RowRect(int r) {
    RECT crc;
    if (!g_list || !GetClientRect(g_list, &crc) || crc.right <= crc.left)
        return RECT{ 0, 0, 0, 0 };
    RECT rc = { crc.left, RowTop(r) - (int)g_scrollPx,
                crc.right, RowTop(r + 1) - (int)g_scrollPx };
    return rc;
}

// 最大可滚动像素
double MaxScrollPx() {
    return (g_totalH > g_viewH) ? (double)(g_totalH - g_viewH) : 0.0;
}

void ListRepaint() {
    if (g_list) InvalidateRect(g_list, nullptr, FALSE);
}

// v0.24：悬浮滚动条自动隐藏 —— 任何滚动/悬停交互（ListScrollTo、热区
// 悬停、抓取拖动）都会「唤醒」thumb：立即显示并重置隐藏倒计时；约
// 0.9 秒无交互且不在拖动/悬停中时自动隐藏（Mac / Win11 悬浮滚动条习惯）
void SbPoke() {
    if (!g_list) return;
    if (MaxScrollPx() <= 0) {            // 内容不超出视口：无可滚，保持隐藏
        if (g_sbTimer) { KillTimer(g_list, 2); g_sbTimer = 0; }
        if (g_sbVis) { g_sbVis = false; ListRepaint(); }
        return;
    }
    if (!g_sbVis) { g_sbVis = true; ListRepaint(); }
    if (g_sbTimer) KillTimer(g_list, 2);
    g_sbTimer = SetTimer(g_list, 2, 900, nullptr);
}

// 滚动到指定像素（animate=true 时启动缓动动画，false 立即到位）
void ListScrollTo(double px, bool animate) {
    SbPoke();                            // v0.24：任何滚动都唤醒 thumb
    double maxPx = MaxScrollPx();
    if (px < 0) px = 0;
    if (px > maxPx) px = maxPx;
    g_scrollTgt = px;
    if (!animate || !g_list) {
        g_scrollPx = px;
        if (g_scrollTimer) { KillTimer(g_list, 1); g_scrollTimer = 0; }
        ListRepaint();
        RepositionRowEditor();               // v0.17：行内编辑框跟随所在行
        KeepRowEditorInView();               // v1.13.3：滚出视口则保存关闭
        return;
    }
    if (g_scrollTimer) return;          // 动画进行中：仅更新目标，帧循环收敛
    g_scrollTimer = SetTimer(g_list, 1, 15, nullptr);
}

// 让指定显示行进入视口（不可见时滚到最近边缘，带动画）
void ListEnsureVisible(int row) {
    if (!g_list || row < 0 || row >= (int)g_rows.size()) return;
    double top = RowTop(row), bot = RowTop(row + 1);
    double px = g_scrollPx;
    if (top < px)                  px = top;
    else if (bot > px + g_viewH)   px = bot - g_viewH;
    else return;
    ListScrollTo(px, true);
}

// 任务行行顶的视口坐标（找不到返回 kAnchorNone）
int RowAnchorY(int idx) {
    if (idx < 0) return kAnchorNone;
    for (size_t k = 0; k < g_rows.size(); ++k)
        if (g_rows[k] == idx)
            return RowTop((int)k) - (int)g_scrollPx;
    return kAnchorNone;
}

// 行结构/行高变化后把该任务行还原到 anchorY —— 通过改写滚动像素实现
void RestoreRowAnchor(int idx, int anchorY) {
    if (idx < 0 || anchorY == kAnchorNone) return;
    for (size_t k = 0; k < g_rows.size(); ++k) {
        if (g_rows[k] != idx) continue;
        double px = (double)(RowTop((int)k) - anchorY);
        if (px < 0) px = 0;
        double maxPx = MaxScrollPx();
        if (px > maxPx) px = maxPx;
        g_scrollPx  = px;
        g_scrollTgt = px;
        if (g_scrollTimer && g_list) { KillTimer(g_list, 1); g_scrollTimer = 0; }
        ListRepaint();
        RepositionRowEditor();
        return;
    }
}

// v1.13.0：点击其他任务或空白处时自动收起已展开的任务（keepIdx 对应任务除外）
static bool CollapseExpandedExcept(int keepIdx) {
    bool changed = false;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        if (g_tasks[i].expanded && (int)i != keepIdx) {
            g_tasks[i].expanded = false;
            changed = true;
        }
    }
    return changed;
}

// 取消列表选中（点击列表空白处 / 分组标题 / 输入框等其他位置时）
void DeselectList() {
    if (g_selRow == -1 && g_multiSel.empty()) return;
    g_selRow = -1;
    g_multiSel.clear();                  // v0.16：多选一并取消
    ListRepaint();
}

// v0.16：任务下标是否在多选集中（绘制高亮 / 右键批量判定用）
static bool MultiHas(int idx) {
    return std::find(g_multiSel.begin(), g_multiSel.end(), idx)
           != g_multiSel.end();
}

// v0.16：Shift+单击 -> [rowA, rowB] 区间内全部任务行加入多选（替换旧多选，
// 分组标题行跳过）
static void MultiSelectRange(int rowA, int rowB) {
    if (rowA > rowB) { int x = rowA; rowA = rowB; rowB = x; }
    g_multiSel.clear();
    for (int r = rowA; r <= rowB && r < (int)g_rows.size(); ++r)
        if (r >= 0 && g_rows[r] >= 0) g_multiSel.push_back(g_rows[r]);
}

// ---------------------------- 行内编辑 / 右键删除 ----------------------------
// 点击位置是否落在任务行的日期时间标签上
static bool DateLabelHit(int idx, const RECT& ir, int x) {
    const Task& t = g_tasks[(size_t)idx];
    std::wstring when = WhenLabelText(t);   // 无日期任务命中「＋ 日期」占位
    if (when.empty()) return false;
    HDC hdc = GetDC(g_list);
    HGDIOBJ ow = SelectObject(hdc, t.done ? g_fontSub : g_fontDate);   // v0.26 与绘制同源
    SIZE sz {};
    GetTextExtentPoint32W(hdc, when.c_str(), (int)when.size(), &sz);
    SelectObject(hdc, ow);
    ReleaseDC(g_list, hdc);
    int right = ir.right - Scale(6) - RowGutter();
    int left  = right - sz.cx - Scale(6);
    return x >= left && x <= right;
}

// v1.12.0：单击是否落在多行任务的「展开 / 收起」按钮上（与 DrawTaskRow 几何一致）
static bool ExpandBtnHit(int idx, const RECT& ir, int x, int y) {
    if (idx < 0 || idx >= (int)g_tasks.size()) return false;
    if (idx == g_rowEditIdx && g_rowEdit) return false;   // v0.17：编辑中按钮被编辑框覆盖
    const Task& t = g_tasks[(size_t)idx];
    int whenW = WhenLabelWidth(t);
    int textRight = ir.right - Scale(12) - RowGutter() - (whenW ? whenW + Scale(12) : 0);
    HDC hdc = GetDC(g_list);
    HGDIOBJ of = SelectObject(hdc, t.done ? g_fontDone : g_font);
    // v1.12.1：按钮存在性按折行结果判定（无 \n 的长文本折行超 5 行同样展开/收起）
    std::vector<std::wstring> lines = WrapTaskText(hdc, t.text,
                                                   textRight - (ir.left + Scale(12) + Scale(18) + Scale(12)));
    if ((int)lines.size() <= kTaskMaxLines) {
        SelectObject(hdc, of);
        ReleaseDC(g_list, hdc);
        return false;
    }
    const wchar_t* label = t.expanded ? L"\u25B4 收起" : L"\u25BE 展开";
    SIZE sz {};
    GetTextExtentPoint32W(hdc, label, (int)wcslen(label), &sz);
    SelectObject(hdc, of);
    ReleaseDC(g_list, hdc);
    int left = textRight - sz.cx;
    // v1.13.5：与绘制同源 —— 行底右下角，行底出视口时吸附到视口底部
    int top = ir.bottom - Scale(6) - Scale(17);
    bool floating = false;
    if (g_viewH > Scale(40) && top + Scale(17) > g_viewH - Scale(4)) {
        top = g_viewH - Scale(4) - Scale(17);
        floating = true;
    }
    if (top < ir.top + Scale(4)) top = ir.top + Scale(4);
    int bot = top + Scale(17);
    if (floating) {          // v1.13.13：胶囊底比文字大一圈，命中区同步扩大
        left -= Scale(8);
        if (left < ir.left) left = ir.left;
        top  -= Scale(4);
        bot  += Scale(4);
    }
    return x >= left && x <= textRight && y >= top && y <= bot;
}

// 悬浮滚动条：右缘 16px 为热区，thumb 宽 7px 圆角胶囊，距右缘 4px
static RECT SbThumbRect() {
    RECT crc;
    if (!g_list || !GetClientRect(g_list, &crc) || crc.bottom <= 0)
        return RECT{ 0, 0, 0, 0 };
    double maxPx = MaxScrollPx();
    int trackLen = crc.bottom - Scale(8);
    if (maxPx <= 0 || trackLen <= Scale(30)) return RECT{ 0, 0, 0, 0 };
    int thumbH = (int)((double)g_viewH * trackLen / g_totalH);
    if (thumbH < Scale(28)) thumbH = Scale(28);
    if (thumbH > trackLen) thumbH = trackLen;
    int maxThumb = trackLen - thumbH;
    int ty = Scale(4) + (int)(g_scrollPx * maxThumb / maxPx);
    int tx = crc.right - Scale(11);
    return RECT{ tx, ty, tx + Scale(7), ty + thumbH };
}

static bool SbHit(int x) {
    RECT crc;
    if (!g_list || !GetClientRect(g_list, &crc)) return false;
    return MaxScrollPx() > 0 && x >= crc.right - Scale(16);
}

// 内容坐标 -> 显示行（键盘翻页用）
static int RowFromContentY(int cy) {
    int n = (int)g_rows.size();
    if (n == 0 || g_rowTops.empty()) return -1;
    if (cy < 0) cy = 0;
    if (cy >= g_totalH) cy = g_totalH - 1;
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (g_rowTops[mid] <= cy) lo = mid; else hi = mid - 1;
    }
    return lo;
}

LRESULT CALLBACK TodoListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_GETDLGCODE:                      // 方向键交给自己（键盘滚动/移动选中）
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_TIMER:
        if (w == 2 && g_sbTimer) {           // v0.24：自动隐藏倒计时到点
            KillTimer(h, 2);
            g_sbTimer = 0;
            if (g_sbVis && (g_sbDrag || g_sbTrack)) {
                g_sbTimer = SetTimer(h, 2, 900, nullptr);  // 交互中：再等一拍
            } else if (g_sbVis) {
                g_sbVis = false;             // 交互结束：自动隐藏
                ListRepaint();
            }
            return 0;
        }
        if (w == 1 && g_scrollTimer) {       // 滚轮缓动动画帧（~60fps）
            g_scrollPx += (g_scrollTgt - g_scrollPx) * 0.45;
            double diff = g_scrollTgt - g_scrollPx;
            if (diff < 0) diff = -diff;
            if (diff < 0.6) {
                g_scrollPx = g_scrollTgt;
                KillTimer(h, 1);
                g_scrollTimer = 0;
            }
            ListRepaint();
            RepositionRowEditor();               // v0.17：滚动帧让行内编辑框跟随所在行
            KeepRowEditorInView();               // v1.13.3：整行滚出视口才关闭
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;                            // 背景由 WM_PAINT 双缓冲统一绘制
    case WM_CTLCOLOREDIT: {                  // v0.24：编辑框底色与选中行同色
        HDC hdc = (HDC)w;                    //（避免白框叠蓝底出现两种颜色）
        if (g_rowEdit && (HWND)l == g_rowEdit) {
            bool done = g_rowEditIdx >= 0 &&
                        g_rowEditIdx < (int)g_tasks.size() &&
                        g_tasks[(size_t)g_rowEditIdx].done;
            SetBkColor(hdc, COL_SEL_BG);
            SetTextColor(hdc, done ? COL_DONE : EffTaskCol());   // v0.27 同源
            return (LRESULT)CachedBrush(COL_SEL_BG);
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT crc;
        GetClientRect(h, &crc);
        int W = crc.right - crc.left, H = crc.bottom - crc.top;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, W > 0 ? W : 1, H > 0 ? H : 1);
        HGDIOBJ ob = SelectObject(mem, bmp);
        FillSolid(mem, crc, COL_WHITE);
        if (!g_rows.empty()) {
            int r0 = RowFromY(crc.top), r1 = RowFromY(crc.bottom - 1);
            if (r0 < 0) r0 = 0;
            for (int r = r0; r <= r1 && r < (int)g_rows.size(); ++r) {
                RECT ir = RowRect(r);
                int v = g_rows[r];
                if (v == ROW_FILL) {
                    FillSolid(mem, ir, COL_WHITE);
                } else if (v == ROW_DONE_SUMMARY) {
                    DrawDoneSummaryRow(mem, ir);
                } else if (v < 0) {
                    DrawHeaderRow(mem, ir, -v - 1);
                } else {
                    // 任务间分隔线：仅当下一行也是任务行时绘制
                    size_t nx = (size_t)r + 1;
                    bool div = nx < g_rows.size() && g_rows[nx] >= 0;
                    // v0.27：整行悬停（未选中时画悬停底色）
                    bool rowHover = (r == g_hoverLine);
                    DrawTaskRow(mem, ir, g_tasks[(size_t)v],
                                (r == g_selRow || MultiHas(v)),   // v0.16：多选高亮
                                rowHover,
                                (r == g_hoverRow && g_hoverBox), div, v);
                }
            }
        }
        // v1.13.11：空列表提示（有任务时不显示）
        if (g_tasks.empty()) {
            SetBkMode(mem, TRANSPARENT);
            HGDIOBJ oe = SelectObject(mem, g_fontSub);
            SetTextColor(mem, COL_MUTED);
            RECT er1 = { crc.left, crc.top + Scale(64), crc.right,
                         crc.top + Scale(64) + Scale(20) };
            DrawTextW(mem, L"还没有任务", -1, &er1,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SetTextColor(mem, COL_GRP_CNT);
            RECT er2 = { crc.left, crc.top + Scale(88), crc.right,
                         crc.top + Scale(88) + Scale(20) };
            DrawTextW(mem, L"在上方输入框写下第一件事，回车即可添加", -1, &er2,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(mem, oe);
        }
        if (MaxScrollPx() > 0 && g_sbVis) {  // v0.24：悬浮滚动条 thumb（自动隐藏）
            RECT tb = SbThumbRect();
            if (tb.bottom > tb.top) {
                COLORREF sc = (g_sbDrag || g_sbTrack) ? COL_SB_HOT : COL_SB;
                HGDIOBJ ob2 = SelectObject(mem, CachedBrush(sc));
                HGDIOBJ op2 = SelectObject(mem, CachedPen(sc));
                RoundRect(mem, tb.left, tb.top, tb.right, tb.bottom,
                          Scale(7), Scale(7));
                SelectObject(mem, op2);
                SelectObject(mem, ob2);
            }
        }
        BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:                            // 视口变化：填塞行/前缀和/滚动钳制
        RebuildRowTops();
        if (g_rowEdit) {                     // v0.23：视口/宽度变了，封顶容量与
            SyncRowEditorSize();             // 折行都随视口重算，编辑框重摆跟随
            RepositionRowEditor();
        }
        ListRepaint();
        return 0;
    case WM_ROWEDITCHK:                      // v0.22：编辑框首帧后复核内容行数
        if (g_rowEdit) SyncRowEditorSize();  //（兜底 EDIT 懒折行的残存时序）
        return 0;
    case WM_MOUSEWHEEL: {                    // 平滑滚动：目标像素 + 缓动动画
        SbPoke();                 // v0.24：滚轮唤醒 thumb（边界处也给反馈）
        // v1.13.3：不再「一滚就退出编辑」—— 编辑框跟着行一起滚（缓动帧里
        // 由 RepositionRowEditor 重摆），只有整行滚出视口才保存关闭
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        ListScrollTo(g_scrollTgt + (double)(-delta) * Scale(96) / WHEEL_DELTA,
                     true);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        CloseRowEditor(true);                // 正在行内编辑则先保存关闭
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        // 悬浮滚动条：点击热区 = 抓取拖动（不算空白位置，不触发自动收起；
        // 不抢焦点，同苹果风格 —— 点滚动条不打断当前输入）
        if (SbHit(x)) {
            CloseRowEditor(true);
            RECT tb = SbThumbRect();
            g_sbDrag = true;
            SbPoke();                        // v0.24：盲抓热区立即显示 thumb
            SetCapture(h);
            g_sbGrabOff = (tb.bottom > tb.top) ? (y - tb.top) : Scale(3);
            ListRepaint();
            return 0;
        }
        SetFocus(h);                         // 点击行区域才接管键盘焦点
        int row = RowFromY(y);
        if (row < 0 || row >= (int)g_rows.size()) { DeselectList(); return 0; }
        int v = g_rows[row];
        if (v == ROW_FILL) {                 // 填塞区 = 空白：取消选中+自动收起
            if (CollapseExpandedExcept(-1)) RefreshListKeepSel();
            DeselectList();
            return 0;
        }
        if (v == ROW_DONE_SUMMARY) {         // v1.13.11：已完成总标题，点击展开/收起
            g_doneCollapsed = !g_doneCollapsed;
            RequestSave();
            RebuildRows();
            DeselectList();
            return 0;
        }
        if (v < 0) {                         // 分组标题：导出热区 / 折叠展开
            int grp = GroupOfRow(v);
            RECT ir = RowRect(row);
            if (HeaderExportHit(grp, ir, x)) { ExportGroup(grp); return 0; }
            ToggleGroup(grp);
            DeselectList();
            return 0;
        }
        int idx = v;
        RECT ir = RowRect(row);
        bool expandBtn = ExpandBtnHit(idx, ir, x, y);
        // v1.13.1：收起（或本行展开/收起）会让上方行高骤减，被点的这一行
        // 整体向上跳 —— 先记住它的视口位置，重建后原样还原，双击落点才不会漂移
        int anchorY = RowAnchorY(idx);
        bool wasExpanded = g_tasks[(size_t)idx].expanded;
        // v1.13.0：点击任务行（非本任务的展开按钮场景）时收起其他已展开任务
        bool chg = CollapseExpandedExcept(idx);
        if (expandBtn) {
            Task& t = g_tasks[(size_t)idx];
            t.expanded = !t.expanded;
            chg = true;
        }
        if (chg) {
            RefreshListKeepSel();
            row = RowOfIdx(idx);
            if (row < 0) return 0;
            ir = RowRect(row);
            if (expandBtn && !wasExpanded) {
                // v1.13.4：展开 -> 行顶滚到视口顶部，从第一行看起
                ListScrollTo((double)RowTop(row), true);
            } else {
                RestoreRowAnchor(idx, anchorY);   // 收起 / 收起他行：行顶原位
            }
        }
        if (!expandBtn) {
            if (PtInBoxZone(ir, x)) {        // 单击勾选框 -> 切换完成状态
                g_multiSel.clear();          // v0.16：勾选属单任务操作，清多选
                g_anchorRow = row;
                g_selRow = row;
                ToggleTask(row);
                return 0;
            }
            if (DateLabelHit(idx, ir, x)) {  // 单击日期标签 -> 修改日期时间
                g_multiSel.clear();          // v0.16：改日期属单任务操作
                g_anchorRow = row;
                g_selRow = row;
                ListRepaint();
                PostMessageW(g_hwnd, WM_OPENDATE, (WPARAM)idx, 0);
                return 0;
            }
        }
        // v0.16：普通单击 / Ctrl+单击 / Shift+单击 三种选中方式
        {
            bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
            if (ctrl) {                      // Ctrl+单击：逐个多选（切换）
                if (g_multiSel.empty() && g_selRow >= 0 &&
                    g_selRow < (int)g_rows.size() && g_rows[g_selRow] >= 0 &&
                    g_rows[g_selRow] != idx)
                    g_multiSel.push_back(g_rows[g_selRow]);   // 原选中为第一项
                auto it = std::find(g_multiSel.begin(), g_multiSel.end(), idx);
                if (it != g_multiSel.end()) g_multiSel.erase(it);
                else g_multiSel.push_back(idx);
                std::sort(g_multiSel.begin(), g_multiSel.end());
                g_selRow    = row;
                g_anchorRow = row;
            } else if (shift) {              // Shift+单击：锚点 -> 本行批量选
                MultiSelectRange((g_anchorRow >= 0) ? g_anchorRow : row, row);
                g_selRow = row;
            } else {                         // 普通单击：单选
                g_multiSel.clear();
                g_selRow    = row;
                g_anchorRow = row;
            }
        }
        ListRepaint();
        return 0;
    }
    case WM_LBUTTONDBLCLK: {                 // 双击任务内容 -> 就地编辑
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        int row = RowFromY(y);
        if (row >= 0 && row < (int)g_rows.size() && g_rows[row] >= 0) {
            int idx = g_rows[row];
            RECT ir = RowRect(row);
            if (!PtInBoxZone(ir, x) && !DateLabelHit(idx, ir, x) &&
                !ExpandBtnHit(idx, ir, x, y)) {
                int anchorY = RowAnchorY(idx);   // v1.13.1：双击落点行保持原位
                if (CollapseExpandedExcept(idx)) {
                    RefreshListKeepSel();
                    RestoreRowAnchor(idx, anchorY);
                }
                row = RowOfIdx(idx);
                if (row >= 0) {
                    POINT hit = { x, y };    // v1.13.3：把点击点交给编辑器定位光标
                    OpenRowEditor(row, hit);
                    g_selRow = row;
                    ListRepaint();
                }
            }
        }
        return 0;
    }
    case WM_CONTEXTMENU: {                   // 右键任务（待办/已完成）-> 菜单删除
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };   // 屏幕坐标
        if (pt.x != -1 || pt.y != -1) {
            POINT cpt = pt;
            ScreenToClient(h, &cpt);
            int row = RowFromY(cpt.y);
            if (row >= 0 && row < (int)g_rows.size() && g_rows[row] >= 0) {
                CloseRowEditor(true);        // 正在行内编辑则先保存关闭
                g_menuRow = row;
                // v0.16：右键目标不在多选集中 -> 改为单选它（在多选集中 ->
                // 保留多选集，菜单提供批量操作）
                if (!MultiHas(g_rows[row])) {
                    g_multiSel.clear();
                    g_selRow    = row;
                    g_anchorRow = row;
                }
                HMENU menu = CreatePopupMenu();
                if (menu) {
                    if (g_multiSel.size() >= 2) {   // v0.16：多选 -> 批量操作
                        wchar_t lab[80];
                        bool anyDone = false;
                        for (int i : g_multiSel)
                            if (g_tasks[(size_t)i].done) { anyDone = true; break; }
                        std::swprintf(lab, 80, L"完成所选（%d 项）",
                                      (int)g_multiSel.size());
                        AppendMenuW(menu, MF_STRING, IDM_MULTI_DONE, lab);
                        if (anyDone) {
                            std::swprintf(lab, 80, L"还原所选（%d 项）",
                                          (int)g_multiSel.size());
                            AppendMenuW(menu, MF_STRING, IDM_MULTI_UNDONE, lab);
                        }
                        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                        std::swprintf(lab, 80, L"删除所选（%d 项）",
                                      (int)g_multiSel.size());
                        AppendMenuW(menu, MF_STRING, IDM_MULTI_DELETE, lab);
                    } else {
                        AppendMenuW(menu, MF_STRING, IDM_ROW_DELETE, L"删除任务");
                        // v0.15：已完成任务额外提供「修改完成日期」
                        if (g_tasks[(size_t)g_rows[row]].done)
                            AppendMenuW(menu, MF_STRING, IDM_ROW_EDITDONE,
                                        L"修改完成日期");
                    }
                    SetForegroundWindow(g_hwnd);
                    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, pt.x, pt.y,
                                     g_hwnd, nullptr);
                    PostMessageW(g_hwnd, WM_NULL, 0, 0);
                    DestroyMenu(menu);
                }
                return 0;
            }
        }
        break;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (g_sbDrag) {                      // 拖动滚动条：像素级映射滚动
            RECT crc;
            GetClientRect(h, &crc);
            double maxPx = MaxScrollPx();
            int trackLen = crc.bottom - Scale(8);
            int thumbH = (int)((double)g_viewH * trackLen / g_totalH);
            if (thumbH < Scale(28)) thumbH = Scale(28);
            if (thumbH > trackLen) thumbH = trackLen;
            int maxThumb = trackLen - thumbH;
            if (maxThumb > 0 && maxPx > 0) {
                int ty = y - g_sbGrabOff - Scale(4);
                ListScrollTo((double)ty * maxPx / maxThumb, false);
            }
            return 0;
        }
        // 滚动条热区悬停状态（thumb 加深；v0.24 悬停热区唤出自动隐藏的 thumb）
        bool overSb = SbHit(x);
        if (overSb) SbPoke();
        if (overSb != g_sbTrack) {
            g_sbTrack = overSb;
            ListRepaint();
        }
        int  row = -1;
        bool box = false;
        bool hand = false;   // 方框或日期标签（含「＋ 日期」占位）上显示手型光标
        int  line = -1;      // v0.27 整行悬停（任意任务行，含勾选框热区之外）
        if (!overSb) {
            int k = RowFromY(y);
            if (k >= 0 && k < (int)g_rows.size()) {
                if (g_rows[k] >= 0) {
                    line = k;                        // v0.27：整行悬停
                    RECT ir = RowRect(k);
                    if (PtInBoxZone(ir, x)) {
                        row = k;
                        box = true;
                        hand = true;
                    } else if (DateLabelHit(g_rows[k], ir, x)) {
                        hand = true;
                    } else if (ExpandBtnHit(g_rows[k], ir, x, y)) {
                        hand = true;         // 「展开 / 收起」手型光标
                    }
                } else if (g_rows[k] != ROW_FILL) {   // 分组标题「↓ 导出」热区
                    RECT ir = RowRect(k);
                    if (HeaderExportHit(GroupOfRow(g_rows[k]), ir, x)) hand = true;
                }
            }
        }
        if (row != g_hoverRow || box != g_hoverBox) {
            int old = g_hoverRow;
            g_hoverRow = row;
            g_hoverBox = box;
            RECT ir;
            if (old >= 0 && old < (int)g_rows.size()) {
                ir = RowRect(old);
                InvalidateRect(h, &ir, FALSE);
            }
            if (row >= 0) {
                ir = RowRect(row);
                InvalidateRect(h, &ir, FALSE);
            }
        }
        if (line != g_hoverLine) {           // v0.27：换行时新旧行区域失效重绘
            int old = g_hoverLine;
            g_hoverLine = line;
            RECT ir;
            if (old >= 0 && old < (int)g_rows.size()) {
                ir = RowRect(old);
                InvalidateRect(h, &ir, FALSE);
            }
            if (line >= 0) {
                ir = RowRect(line);
                InvalidateRect(h, &ir, FALSE);
            }
        }
        SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
        if (!g_trackMouse) {
            TRACKMOUSEEVENT te { sizeof(te), TME_LEAVE, h, 0 };
            if (TrackMouseEvent(&te)) g_trackMouse = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trackMouse = false;
        if (g_sbTrack) { g_sbTrack = false; ListRepaint(); }
        if (g_hoverRow >= 0 && g_hoverRow < (int)g_rows.size()) {
            RECT ir = RowRect(g_hoverRow);
            InvalidateRect(h, &ir, FALSE);
        }
        g_hoverRow = -1;
        g_hoverBox = false;
        if (g_hoverLine >= 0 && g_hoverLine < (int)g_rows.size()) {
            RECT ir = RowRect(g_hoverLine);   // v0.27：整行悬停同步失效
            InvalidateRect(h, &ir, FALSE);
        }
        g_hoverLine = -1;
        return 0;
    case WM_CAPTURECHANGED:
        if (g_sbDrag) { g_sbDrag = false; ListRepaint(); }
        return 0;
    case WM_LBUTTONUP:
        if (g_sbDrag) {
            g_sbDrag = false;
            if (GetCapture() == h) ReleaseCapture();
            ListRepaint();
            return 0;
        }
        return 0;
    case WM_KEYDOWN: {
        int cnt = (int)g_rows.size();
        auto moveSel = [&](int nr) {
            if (nr < 0 || nr >= cnt || nr == g_selRow) return;
            CloseRowEditor(true);
            g_selRow    = nr;
            g_anchorRow = nr;                // v0.16：键盘移动重设批量选锚点
            g_multiSel.clear();              // v0.16：键盘导航回到单选
            ListEnsureVisible(nr);
            ListRepaint();
        };
        switch (w) {
        case VK_SPACE: {                     // 空格 = 切换完成 / 折叠分组
            if (g_selRow >= 0 && g_selRow < cnt) {
                if (g_rows[g_selRow] == ROW_FILL) return 0;
                if (g_rows[g_selRow] < 0) ToggleGroup(GroupOfRow(g_rows[g_selRow]));
                else ToggleTask(g_selRow);
            }
            return 0;
        }
        case VK_DELETE:                      // v2.3.2：Delete 删除所选（多选批量删除）
            DeleteSelected();
            return 0;
        case VK_UP:
        case VK_LEFT:
            moveSel(g_selRow < 0 ? cnt - 1 : g_selRow - 1);
            return 0;
        case VK_DOWN:
        case VK_RIGHT:
            moveSel(g_selRow < 0 ? 0 : g_selRow + 1);
            return 0;
        case VK_HOME:
            moveSel(cnt > 0 ? 0 : -1);
            return 0;
        case VK_END:
            moveSel(cnt - 1);
            return 0;
        case VK_PRIOR: {                     // PgUp：上移一屏并对齐到新顶行
            if (cnt <= 0) return 0;
            int cy = (int)g_scrollPx - g_viewH;
            if (cy < 0) cy = 0;
            ListScrollTo(cy, true);
            moveSel(RowFromContentY(cy));
            return 0;
        }
        case VK_NEXT: {                      // PgDn：下移一屏并对齐到新顶行
            if (cnt <= 0) return 0;
            int cy = (int)g_scrollPx + g_viewH;
            double maxPx = MaxScrollPx();
            if (cy > (int)maxPx) cy = (int)maxPx;
            ListScrollTo(cy, true);
            moveSel(RowFromContentY(cy));
            return 0;
        }
        case 'A':                            // v0.16：Ctrl+A 全选全部任务
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                g_multiSel.clear();
                for (int r = 0; r < cnt; ++r)
                    if (g_rows[r] >= 0) g_multiSel.push_back(g_rows[r]);
                CloseRowEditor(true);
                ListRepaint();
                return 0;
            }
            break;
        }
        break;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
