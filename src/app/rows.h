// 行模型：分组归属、展开状态、显示行重建。
// 由领域数据算出可绘制的显示行，并驱动列表重绘与滚动位置。
#pragma once

#include "common.h"

extern std::vector<int>  g_rows;

extern UINT   g_fillH;

static const int ROW_FILL = -100;

static const int ROW_DONE_SUMMARY = -11;   // v1.13.11：已完成汇总行（收纳五个子分组）

extern bool g_doneCollapsed;

extern bool g_showWeek;

extern bool g_showLastWeek;

extern bool g_showMonth;

extern bool g_showLastMonth;

extern bool g_showOther;

extern bool g_showOverdue;

extern bool g_showToday;

extern bool g_showTomorrow;

extern bool g_showIn2;

extern bool g_showNoDate;

extern int  g_grpCount[5];

extern int  g_openCount[5];

int CountDone();

bool InGroupDone(long long doneAt, int grp);

const wchar_t* GroupName(int grp);

const wchar_t* OpenGroupName(int grp);

bool GroupOpen(int grp);

int RowOfIdx(int idx);

void RebuildRows();            // 前置声明（定义在后）

void RefreshListKeepSel();

void ToggleGroup(int grp);

void RefreshList();
