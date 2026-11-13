// 划词结果气泡（淡入淡出）。
#pragma once

#include "common.h"

extern HWND g_hwndToast;

LRESULT CALLBACK ToastProc(HWND h, UINT m, WPARAM w, LPARAM l);

void ShowToast(const std::wstring& title, const std::wstring& body, bool ok);
