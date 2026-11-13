// 系统深色模式接入
// uxtheme.dll 的逆序导出（135/136/104）没有公开头文件，这里动态加载：
// 取不到（Win7/8）就整体静默降级，不影响任何既有行为。
//
// 做的事：
//   1) 进程级首选外观：跟随 Tickly 自己的主题（深色 = ForceDark，浅色 = ForceLight），
//      并刷新菜单主题 —— 托盘菜单 /「⋯」菜单 / 右键菜单才会跟着变深；
//   2) 窗口级：DWM 深色标题栏 + 给系统控件换深色主题名
//      （Edit/ComboBox/HotKey → DarkMode_CFD，DateTimePicker/ScrollBar →
//       DarkMode_Explorer），否则设置/日期/提醒对话框里会出现白色输入框。
#pragma once

#include "common.h"

// 进程级：按当前主题设置首选外观并刷新菜单主题（ApplyTheme 时调用）
void DarkModeInit();

// 窗口级：深色标题栏 + 子控件深色主题（每个对话框 WM_INITDIALOG 时调用）
void DarkModeApplyToWindow(HWND hwnd);

// 只要深色标题栏（主窗口用：子控件都是自绘或走 WM_CTLCOLOR，不需要 uxtheme）
void DarkModeApplyTitleBar(HWND hwnd);

// 主题切换后：主窗口 + 所有打开的顶层窗口重新应用
void DarkModeRefresh();
