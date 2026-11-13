// 行模型：分组归属、展开状态、显示行重建
#include "common.h"
#include "app/rows.h"
#include "ui/list_view.h"
#include "app/store.h"
#include "core/task.h"
#include "core/time_util.h"
#include "ui/main_window.h"

std::vector<int>  g_rows;      // 显示行 -> 任务下标；负值 = 分组标题行
UINT   g_fillH   = 0;            // 填塞行当前高度（像素，随窗口尺寸变化）
bool g_doneCollapsed = true;        // v1.13.11：已完成区整体收纳（持久化）
// g_fillRow / g_fillH 已在滚动状态块中声明（v1.13.0）
bool g_showWeek      = true;   // 完成分组展开状态（持久化）
bool g_showLastWeek  = true;
bool g_showMonth     = true;
bool g_showLastMonth = true;
bool g_showOther     = true;
// v0.15 未完成分组展开状态（持久化）：已逾期 / 今天 / 明天 / 更晚 / 无日期
bool g_showOverdue   = true;
bool g_showToday     = true;
bool g_showTomorrow  = true;
bool g_showIn2       = true;
bool g_showNoDate    = true;
int  g_grpCount[5] = {0, 0, 0, 0, 0};  // 完成分组各桶数（绘制用）
int  g_openCount[5] = {0, 0, 0, 0, 0}; // 未完成各分组数（v1.11.0，绘制用）

// ---------------------------- 行模型 ----------------------------------------
int CountDone() {
    int n = 0;
    for (const Task& t : g_tasks) if (t.done) ++n;
    return n;
}

// 完成时间是否属于分组 grp（桶允许重叠：本周完成的任务同时计入本月完成）
// 0=本周 1=上周 2=本月（含本周） 3=上月 4=其他（不属于以上任一档）
bool InGroupDone(long long doneAt, int grp) {
    switch (grp) {
    case 0:  return doneAt >= ThisWeekStart();
    case 1:  return doneAt >= LastWeekStart()  && doneAt < ThisWeekStart();
    case 2:  return doneAt >= ThisMonthStart();
    case 3:  return doneAt >= LastMonthStart() && doneAt < ThisMonthStart();
    default: return doneAt < LastWeekStart()   && doneAt < LastMonthStart();
    }
}

const wchar_t* GroupName(int grp) {
    static const wchar_t* kName[5] =
        { L"本周完成", L"上周完成", L"本月完成", L"上月完成", L"其他" };
    return kName[grp];
}

// v1.11.0 未完成分组（grp 5..9）名称与计数
const wchar_t* OpenGroupName(int grp) {
    static const wchar_t* kName[5] =
        { L"已逾期", L"今天", L"明天", L"更晚", L"无日期" };
    return kName[grp - 5];
}

// v0.15：grp 0..4 = 完成分组，5..9 = 未完成分组（已逾期/今天/明天/更晚/无日期），
// 两组都支持折叠 / 展开
bool GroupOpen(int grp) {
    switch (grp) {
    case 0:  return g_showWeek;
    case 1:  return g_showLastWeek;
    case 2:  return g_showMonth;
    case 3:  return g_showLastMonth;
    case 4:  return g_showOther;
    case 5:  return g_showOverdue;
    case 6:  return g_showToday;
    case 7:  return g_showTomorrow;
    case 8:  return g_showIn2;
    default: return g_showNoDate;
    }
}

static void SetGroupOpen(int grp, bool v) {
    switch (grp) {
    case 0:  g_showWeek = v;      break;
    case 1:  g_showLastWeek = v;  break;
    case 2:  g_showMonth = v;     break;
    case 3:  g_showLastMonth = v; break;
    case 4:  g_showOther = v;     break;
    case 5:  g_showOverdue = v;   break;
    case 6:  g_showToday = v;     break;
    case 7:  g_showTomorrow = v;  break;
    case 8:  g_showIn2 = v;       break;
    default: g_showNoDate = v;    break;
    }
}

// 任务下标 -> 当前显示行（找不到返回 -1）
int RowOfIdx(int idx) {
    for (size_t k = 0; k < g_rows.size(); ++k)
        if (g_rows[k] == idx) return (int)k;
    return -1;
}

void RebuildRows() {
    g_hoverRow = -1;   // 行结构变化，悬停状态失效
    g_hoverBox = false;
    g_rows.clear();
    g_grpCount[0] = g_grpCount[1] = g_grpCount[2] = g_grpCount[3] = g_grpCount[4] = 0;
    for (int g = 0; g < 5; ++g) g_openCount[g] = 0;
    // 未完成任务按开始日期分组（v1.11.0）：已逾期 / 今天 / 明天 / 更晚 / 无日期，
    // 空组自动隐藏。组内排序沿用 v1.9.2：有日期的按日期时间正序（同刻保持添加
    // 序），无日期的按添加时间倒序（g_tasks 数组本身即添加倒序，保持原相对顺序）
    long long todayMid = TodayMidnight();
    std::vector<int> openBuckets[5];   // 0=逾期 1=今天 2=明天 3=更晚 4=无日期
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        if (g_tasks[i].done) continue;
        long long due = g_tasks[i].due;
        int g;
        if (!due)                          g = 4;
        else if (due < todayMid)           g = 0;
        else if (due < todayMid + 86400)   g = 1;
        else if (due < todayMid + 2 * 86400) g = 2;
        else                               g = 3;
        openBuckets[g].push_back((int)i);
        ++g_openCount[g];
    }
    for (int g = 0; g < 5; ++g) {
        if (g != 4)
            std::stable_sort(openBuckets[g].begin(), openBuckets[g].end(),
                [](int a, int b) {
                    return g_tasks[(size_t)a].due < g_tasks[(size_t)b].due;
                });
        if (openBuckets[g].empty()) continue;   // 分组内没有任务时自动隐藏
        g_rows.push_back(-(g + 6));             // 未完成分组标题行：-6..-10
        if (GroupOpen(g + 5))                   // v0.15：未完成分组可折叠
            for (int i : openBuckets[g]) g_rows.push_back(i);
    }
    // v1.10.0 填塞行：把分组区推到列表底部（折叠时贴底，展开向上生长）
    g_rows.push_back(ROW_FILL);
    g_fillRow = (int)g_rows.size() - 1;
    std::vector<int> buckets[5];
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        if (!g_tasks[i].done) continue;
        for (int g = 0; g < 5; ++g) {
            if (!InGroupDone(g_tasks[i].doneAt, g)) continue;
            buckets[g].push_back((int)i);   // 桶重叠：同一任务可进多个分组
            ++g_grpCount[g];
        }
    }
    // v1.13.11：五个已完成分组默认收成一行「已完成 N」—— 之前 5 个空分组
    // 常驻占掉列表三分之一高度，全是 0，是界面最大的视觉噪音
    g_rows.push_back(ROW_DONE_SUMMARY);        // 汇总行常驻（点击展开/收起）
    if (!g_doneCollapsed) {
        for (int g = 0; g < 5; ++g) {
            std::stable_sort(buckets[g].begin(), buckets[g].end(),
                [](int a, int b) {
                    return g_tasks[(size_t)a].doneAt > g_tasks[(size_t)b].doneAt;
                });
            g_rows.push_back(-(g + 1));   // 分组标题行（展开后空分组也显示）
            if (GroupOpen(g))
                for (int i : buckets[g]) g_rows.push_back(i);
        }
    }
    // v1.13.0：自绘列表 —— 重建行 top 前缀和；选中行结构与 ListBox 版
    // ResetContent 一样被清空（调用方需要时自行恢复）
    g_selRow = -1;
    RebuildRowTops();
    ListRepaint();
}

// v1.12.0：重建行并尽量保留选中任务（多行展开/收起、行内编辑保存后调用）
void RefreshListKeepSel() {
    int idx = -1;
    if (g_selRow >= 0 && g_selRow < (int)g_rows.size() && g_rows[g_selRow] >= 0)
        idx = g_rows[g_selRow];
    RebuildRows();
    if (idx >= 0) {
        int k = RowOfIdx(idx);
        if (k >= 0) g_selRow = k;
    }
    UpdateButtons();
}

// v0.15：完成分组（0..4）与未完成分组（5..9）都可折叠
void ToggleGroup(int grp) {
    if (grp < 0 || grp >= 10) return;
    bool opening = !GroupOpen(grp);
    SetGroupOpen(grp, opening);
    SaveTasks();
    RebuildRows();
    // 完成分组区底部锚定：展开时内容向上生长出视口，此前列表停在原位，
    // 看起来像「跳回顶部」（展开的组其实在视口外）—— 现在把组标题滚到
    // 视口顶部，展开的任务直接可见；未完成分组在列表顶部、内容向下
    // 生长，无需调整滚动
    if (opening && grp < 5) {
        for (size_t r = 0; r < g_rows.size(); ++r) {
            if (g_rows[r] == -(grp + 1)) { ListScrollTo(RowTop((int)r), true); break; }
        }
    }
    UpdateButtons();
}

// ---------------------------- UI 动作 ---------------------------------------
void RefreshList() { RebuildRows(); }
