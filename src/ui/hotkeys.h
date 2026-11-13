// 全局快捷键注册与展示。
#pragma once

#include "common.h"

extern UINT g_hkShowMod;

extern UINT g_hkShowVk;

extern UINT g_hkCapMod;

extern UINT g_hkCapVk;

static const UINT HK_ID_SHOW = 11;   // 显示主窗口

static const UINT HK_ID_CAP  = 12;   // 划词添加任务

// 主键展示名（不含修饰键）："Q" "F1" "Space"…
std::wstring HotkeyKeyName(UINT vk);

void ApplyHotkeys();

std::wstring HotkeyStatusText();
