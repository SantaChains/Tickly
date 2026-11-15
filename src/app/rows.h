// 行模型：分组归属、展开状态、显示行重建。
// 由领域数据算出可绘制的显示行，并驱动列表重绘与滚动位置。
#pragma once

#include "common.h"

extern std::vector<int>  g_rows;

extern UINT   g_fillH;

static const int ROW_FILL = -100;

static const int ROW_DONE_SUMMARY = -11;   // v1.13.11：已完成汇总行（收纳五个子分组）

// 行编码：g_rows 里 >=0 为任务下标，负数一律是结构行：
//   -1..-5 完成分组标题（组 0..4）、-6..-10 未完成分组标题（组 5..9）、
//   -11 完成汇总、-100 填塞。下面的判定函数是唯一解码入口，不要在别处算负数。
inline bool IsTaskRow(int v)     { return v >= 0; }

inline bool IsFillRow(int v)     { return v == ROW_FILL; }

inline bool IsDoneSummaryRow(int v) { return v == ROW_DONE_SUMMARY; }

inline bool IsGroupHeaderRow(int v) { return v >= -10 && v <= -1; }

inline int GroupOfRow(int v) { return IsGroupHeaderRow(v) ? -v - 1 : -1; }

inline int HeaderRowForGroup(int grp) { return -(grp + 1); }   // 组 0..9 -> -1..-10

extern bool g_doneCollapsed;

extern bool g_groupOpen[10];   // 展开状态：0..4 完成分组，5..9 未完成分组

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
