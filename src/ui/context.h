// 界面上下文：主窗口与控件句柄、字体/画刷、DPI、布局矩形、悬停态。
// 只放跨模块都要读的界面上下文；领域状态留在各自模块里。
#pragma once

#include "common.h"

extern bool     g_wpSaved;

extern bool     g_wpMax;

extern RECT     g_wpRect;

extern UINT_PTR g_wpTimer;

extern bool g_closeToTray;

extern HWND   g_hwnd;

extern HWND   g_input;

extern HWND   g_list;

extern HWND   g_btnAdd;

extern HWND   g_btnMore;

extern HFONT  g_fontTitle;

extern HFONT  g_fontSub;

extern HFONT  g_fontGroup;

extern HFONT  g_fontGroupBold;

extern HFONT  g_fontGroupDone;

extern HFONT  g_fontDate;

extern HFONT  g_font;

extern HFONT  g_fontDone;

extern HFONT  g_fontRemindT;

extern HFONT  g_fontRemindB;

extern HFONT  g_fontToastT;

extern HFONT  g_fontToastB;

extern HBRUSH g_brWhite;

extern HBRUSH g_brBg;

extern HPEN   g_penCheck;

extern WNDPROC g_oldEditProc;

extern bool    g_imeComposing;

extern bool    g_rowImeComposing;

extern bool    g_btnAddHot;

extern WNDPROC g_oldBtnAddProc;

extern bool    g_btnMoreHot;

extern WNDPROC g_oldBtnMoreProc;

extern UINT   g_dpi;

extern RECT   g_listFrame;
