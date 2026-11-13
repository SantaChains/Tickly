// 行内编辑：编辑框几何、多行生长、自绘蓝色光标。
#pragma once

#include "common.h"

extern HWND    g_rowEdit;

extern int     g_rowEditRow;

extern int     g_rowEditIdx;

extern int     g_rowEditLines;

extern int     g_menuRow;

void CloseRowEditor(bool save);

void HideSystemCaret(HWND h);

void PaintBlueCaret(HWND h);

void AttachBlueCaret(HWND h);

void DetachBlueCaret();

void CaretKick(HWND h);

void CaretBlink(HWND h);

void KeepRowEditorInView();          // v1.13.3：编辑框被滚出视口时自动收尾

void RepositionRowEditor();          // v0.17：滚动 / 行高变化后让编辑框跟随所在行

void SyncRowEditorSize();

void OpenRowEditor(int row, POINT clickPt);
