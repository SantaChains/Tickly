// 输入框：子类化、占位文案、光标垂直居中、实时识别提示。
#pragma once

#include "common.h"

extern RECT   g_hintRect;

extern std::wstring g_hintText;

extern RECT   g_inputFrame;

extern int g_inputH;

void InvalidateInputFmtRect();

void UpdateInputHint();

void UpdateInputSize();

void CenterInput(bool force = false);   // 前置声明（v1.12.0 输入框行数变化时重算内边距）

LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l);
