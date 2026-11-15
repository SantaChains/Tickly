// 任务文本测量：折行、行高与行高缓存（与列表几何 / 绘制分离）
#include "common.h"
#include "ui/list_view.h"
#include "ui/gdi_util.h"
#include "ui/row_editor.h"
#include "ui/context.h"
#include "ui/theme.h"
#include "app/store.h"
#include "core/task.h"
#include "core/time_util.h"

#include <cstdint>

// 列表视口宽（RebuildRowTops 记录，行高测量复用）
static int s_listW = 0;

void SetListWidth(int w) { s_listW = w; }

// 把一个词放入当前行；放不下时换行；单词本身超宽（如长 URL、无空格的
// 中文长段落）按字符硬断 —— 每行用一次 GetTextExtentExPointW 直接取
// 「maxW 内可容纳的前缀字符数」，不再对每个候选前缀各做一次
// GetTextExtentPoint32 二分测量（长文本折行是冷启动的主要开销）
void PushWrappedWord(HDC hdc, const std::wstring& word, int maxW,
                            std::wstring& cur, std::vector<std::wstring>& lines) {
    if (word.empty()) return;
    // s 中宽度不超过 maxW 的最长前缀字符数（至少 1：单字符也超宽时只能硬留）
    auto fitPrefix = [&](const wchar_t* s, int len) -> int {
        int fit = 0;
        SIZE sz {};
        GetTextExtentExPointW(hdc, s, len, maxW, &fit, nullptr, &sz);
        if (fit > len) fit = len;
        return fit > 0 ? fit : 1;
    };
    std::wstring cand = cur.empty() ? word : cur + L' ' + word;
    if (fitPrefix(cand.c_str(), (int)cand.size()) >= (int)cand.size()) {
        cur = cand;
        return;
    }
    if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
    size_t pos = 0;
    for (;;) {
        size_t rem = word.size() - pos;
        size_t fit = (size_t)fitPrefix(word.c_str() + pos, (int)rem);
        if (fit >= rem) {                        // 剩余部分整段放得下
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
    std::uint64_t            sig = 0;   // 0 = 未缓存；非 0 = 文本+宽度+字号指纹
    std::vector<std::wstring> lines;
};

// 按任务下标缓存：命中只需比一次指纹，不必对全文建哈希表键（此前以整段文本
// 为 unordered_map 键，长文本每次查找都要拷贝 + 哈希一遍全文）
std::vector<WrapEntry> g_wrapByTask;

std::uint64_t WrapSig(const std::wstring& text, int maxW, int fontPx) {
    std::uint64_t h = 1469598103934665603ull;          // FNV-1a 64
    auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (wchar_t c : text) mix((std::uint64_t)c);
    mix((std::uint64_t)(std::uint32_t)maxW);
    mix((std::uint64_t)(std::uint32_t)fontPx);
    return h | 1;                                     // 保证非 0
}

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
const std::vector<std::wstring>* WrapCached(HDC hdc, int idx, int maxW) {
    if (maxW < Scale(20)) maxW = Scale(20);
    if (idx < 0 || idx >= (int)g_tasks.size()) return nullptr;
    if (g_wrapByTask.size() != g_tasks.size())
        g_wrapByTask.assign(g_tasks.size(), WrapEntry{});
    // 文本改动后签名随之变化，同签名必然同折行结果
    const std::wstring& text = g_tasks[(size_t)idx].text;
    WrapEntry& e = g_wrapByTask[(size_t)idx];
    const std::uint64_t sig = WrapSig(text, maxW, TaskLineHeight(hdc));
    if (e.sig != sig) {
        e.lines = WrapTaskTextCompute(hdc, text, maxW);
        e.sig   = sig;
    }
    return &e.lines;
}

}  // namespace

std::vector<std::wstring> WrapTaskText(HDC hdc, int idx, int maxW) {
    const std::vector<std::wstring>* ls = WrapCached(hdc, idx, maxW);
    return ls ? *ls : std::vector<std::wstring> { L"" };
}

int WrapTaskLineCount(HDC hdc, int idx, int maxW) {
    const std::vector<std::wstring>* ls = WrapCached(hdc, idx, maxW);
    return ls ? (int)ls->size() : 1;
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

// 行高按任务缓存：签名覆盖所有影响行高的输入（文本、列表宽、字号/DPI、
// 完成态、展开态、截止时间与是否含时刻）。命中就不再做折行与 GDI 度量，
// 勾选/删除后的全量重建从「重算 N 行」变成「查 N 次表」。
// 下标会随增删漂移，但签名不同就不会误命中（同签名必然同高）。
namespace {
struct RowHeightMemo {
    std::uint64_t sig = 0;
    int           h   = 0;
};
std::vector<RowHeightMemo> g_heightMemo;
}  // namespace

static std::uint64_t TaskHeightSig(const Task& t, int listW) {
    std::uint64_t h = 1469598103934665603ull;          // FNV-1a 64
    auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (wchar_t c : t.text) { h = (h ^ (std::uint64_t)c) * 1099511628211ull; }
    mix((std::uint64_t)(std::uint32_t)listW);
    mix((std::uint64_t)(std::uint32_t)g_taskFontSize);
    mix((std::uint64_t)g_dpi);
    mix((std::uint64_t)t.due);
    mix(t.hasTime ? 1u : 0u);
    mix(t.done     ? 1u : 0u);
    mix(t.expanded ? 1u : 0u);
    return h;
}

// 任务行高度：单行任务固定 Scale(40)（与旧版一致）；
// 多行任务 = 上下内边距 + 可见行数 x 行高（未展开 > 5 行时另留「展开」按钮行）
static int TaskRowHeightCompute(const Task& t, int idx, int listW) {
    int whenW = WhenLabelWidth(t);
    int textLeft  = Scale(12) + Scale(18) + Scale(12);
    int textRight = listW - Scale(12) - RowGutter() - (whenW ? whenW + Scale(12) : 0);
    HDC hdc = MeasureDC();
    HGDIOBJ of = SelectObject(hdc, t.done ? g_fontDone : g_font);
    int lineH = TaskLineHeight(hdc);
    int total = WrapTaskLineCount(hdc, idx, textRight - textLeft);
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

int TaskRowHeight(int idx) {
    if (idx < 0 || idx >= (int)g_tasks.size()) return Scale(40);
    const Task& t = g_tasks[(size_t)idx];
    int listW = s_listW;
    if (listW <= 0) {                    // 尚未记录视口宽（首次布局前）时现取
        RECT lrc;
        if (!g_list || !GetClientRect(g_list, &lrc)) return Scale(40);
        listW = lrc.right;
    }
    if (listW <= 0) return Scale(40);

    // 正在行内编辑的这一行高度取决于 EDIT 实测行数，不入缓存
    if (idx == g_rowEditIdx && g_rowEdit != nullptr)
        return TaskRowHeightCompute(t, idx, listW);

    if (g_heightMemo.size() != g_tasks.size())
        g_heightMemo.resize(g_tasks.size());
    RowHeightMemo& m = g_heightMemo[(size_t)idx];
    const std::uint64_t sig = TaskHeightSig(t, listW);
    if (m.h > 0 && m.sig == sig) return m.h;
    m.sig = sig;
    m.h   = TaskRowHeightCompute(t, idx, listW);
    return m.h;
}
