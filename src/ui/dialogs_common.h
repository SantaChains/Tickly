// 内存对话框模板：用代码拼对话框（无 .rc 依赖）。
#pragma once

#include "common.h"

struct DlgItem {
    DWORD          style;
    WORD           x, y, w, h;
    WORD           id;
    WORD           cls;      // 0x0080 button / 0x0081 edit / 0x0082 static
    const wchar_t* text;
    const wchar_t* clsName;  // 自定义窗口类名（cls==0 时使用，如日期选择器）
};

typedef std::vector<int> DlgBtnIds;

static const WORD CLS_BUTTON = 0x0080;

static const WORD CLS_EDIT   = 0x0081;

static const WORD CLS_STATIC = 0x0082;

DLGTEMPLATE* BuildDialog(const wchar_t* title, int cx, int cy,
                                const std::vector<DlgItem>& items,
                                DWORD exstyle = 0);

void GrowDialogToFitText(HWND h, int textId,
                                const std::vector<int>& btnIds,
                                int maxTextH = 0, bool forEdit = false);
