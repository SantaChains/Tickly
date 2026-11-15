// 主窗口：布局、标题区绘制、消息分发。
#pragma once

#include "common.h"

void Layout();         // 前置声明（v1.12.0 输入框长高时重排布）

void RequestSave();    // 合并落盘：300ms 内的连续变更只写一次盘

void FlushSave();      // 立即落盘（退出/关机/拖动结束前调用）

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
