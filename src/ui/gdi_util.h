// GDI 工具：DPI 缩放、字体创建、画刷/画笔缓存。
#pragma once

#include "common.h"

int Scale(int v);

int RowGutter();

void InvalidateGdiCaches();

HBRUSH CachedBrush(COLORREF c);

HPEN CachedPen(COLORREF c);

void FillSolid(HDC hdc, const RECT& rc, COLORREF c);

HFONT MakeFont(int pxHeight, bool bold = false, bool strike = false);

void CreateUiResources();
