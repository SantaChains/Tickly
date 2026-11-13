// DWM 窗口外壳：Win11 圆角、深色标题栏、边框色。
#pragma once

#include "common.h"

// 主窗口外壳（圆角 + 标题栏/边框颜色 + 深色标题栏）
void ApplyDwmTheme();

// 任意窗口的深色标题栏（对话框也用它；加载失败时静默降级）
void DwmSetDarkTitleBar(HWND hwnd, bool dark);
