// 外观主题：Light/Dark 调色板、字体设置、主题切换。
#pragma once

#include "common.h"

extern int      g_taskFontSize;

extern COLORREF g_taskFontCol;

extern int      g_grpFontSize;

extern COLORREF g_grpFontCol;

extern int      g_doneGrpFontSize;

extern COLORREF g_doneGrpFontCol;

extern int      g_dtFontSize;

extern COLORREF g_dtFontCol;

extern COLORREF COL_ACCENT;

extern COLORREF COL_HIGHLIGHT;

extern COLORREF COL_OVERDUE;

extern COLORREF COL_TEXT;

extern COLORREF COL_MUTED;

extern COLORREF COL_DONE;

extern COLORREF COL_GRP_CNT;

extern COLORREF COL_BORDER;

extern COLORREF COL_BOX_BD;

extern COLORREF COL_BG;

extern COLORREF COL_SEL_BG;

extern COLORREF COL_WHITE;

extern COLORREF COL_SB;

extern COLORREF COL_SB_HOT;

extern COLORREF COL_HOVER_BG;

extern int  g_themeMode;

extern int  g_themeDark;

COLORREF MixColor(COLORREF a, COLORREF b, double t);

COLORREF DarkenColor(COLORREF c, int pct);

COLORREF EffTaskCol();

COLORREF EffDoneGrpCol();

COLORREF EffGrpCol();

COLORREF EffDtCol();

void ApplyTheme();

void ApplyFontSettings();
