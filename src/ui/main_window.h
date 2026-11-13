// 主窗口：布局、标题区绘制、消息分发。
#pragma once

#include "common.h"

void UpdateButtons();  // 前置声明（早于定义使用）

void Layout();         // 前置声明（v1.12.0 输入框长高时重排布）

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
