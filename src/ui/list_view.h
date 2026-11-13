// 自绘任务列表：行几何、平滑滚动、绘制与鼠标交互。
// 绘制实现见 ui/list_paint.cpp。
#pragma once

#include "common.h"
#include "core/task.h"

extern int    g_selRow;

extern std::vector<int> g_multiSel;

extern int g_anchorRow;

extern double g_scrollPx;

extern double g_scrollTgt;

extern UINT_PTR g_scrollTimer;

extern std::vector<int> g_rowTops;

extern int    g_totalH;

extern int    g_viewH;

extern bool   g_sbVis;

extern UINT_PTR g_sbTimer;

extern int    g_fillRow;

extern int    g_hoverRow;

extern bool   g_hoverBox;

static const int kTaskMaxLines = 5;   // 未展开时最多显示 5 行

static const int kAnchorNone = -1000000;

void PushWrappedWord(HDC hdc, const std::wstring& word, int maxW,
                            std::wstring& cur, std::vector<std::wstring>& lines);

std::vector<std::wstring> WrapTaskText(HDC hdc, const std::wstring& text,
                                              int maxW);

// 只要行数（行高计算用：命中缓存时不拷贝结果）
int WrapTaskLineCount(HDC hdc, const std::wstring& text, int maxW);

int WhenLabelWidth(const Task& t);

int TaskLineHeight(HDC hdc);

int RowEditMaxLines(int contentLines);

int TaskRowHeight(int idx);

int RowHeightAt(int r);

UINT CalcFillerHeight();

void RebuildRowTops();

int RowTop(int r);

RECT RowRect(int r);

double MaxScrollPx();

void ListRepaint();

void SbPoke();

void ListScrollTo(double px, bool animate);

void ListEnsureVisible(int row);

int RowAnchorY(int idx);

void RestoreRowAnchor(int idx, int anchorY);

void DeselectList();

LRESULT CALLBACK TodoListProc(HWND h, UINT m, WPARAM w, LPARAM l);

void DrawHeaderRow(HDC hdc, const RECT& rc, int grp);

void DrawDoneSummaryRow(HDC hdc, const RECT& rc);   // v1.13.11

void DrawTaskRow(HDC hdc, const RECT& rc, const Task& t, bool sel,
                        bool rowHover, bool hoverBox, bool divider, int idx);
