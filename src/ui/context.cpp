// 界面上下文：窗口/控件句柄、字体、DPI、布局矩形、悬停态
#include "common.h"
#include "ui/context.h"
#include "ui/list_view.h"
#include "ui/row_editor.h"


// 窗口位置记忆（tasks.txt 的 W 行）
bool     g_wpSaved = false;
bool     g_wpMax   = false;
RECT     g_wpRect  {};
UINT_PTR g_wpTimer = 0;   // 延时落盘定时器（覆盖贴靠/最大化等非拖动尺寸变化）
bool g_closeToTray = true;     // 点 X 后缩到托盘后台运行（设置可改，持久化）

// ---------------------------- 全局控件 -------------------------------------
HWND   g_hwnd      = nullptr;
HWND   g_input     = nullptr;
HWND   g_list      = nullptr;
HWND   g_btnAdd    = nullptr;

HWND   g_btnMore   = nullptr;   // v1.13.11：右上角「⋯」菜单按钮

HFONT  g_fontTitle = nullptr;
HFONT  g_fontSub   = nullptr;
HFONT  g_fontGroup = nullptr;   // v1.13.0 分组标题字体（大小/颜色可设置）
HFONT  g_fontGroupBold = nullptr; // v0.25 未完成分组标题加粗字体
HFONT  g_fontGroupDone = nullptr; // v1.13.6 已完成分组标题字体
HFONT  g_fontDate = nullptr;      // v0.26 日期时间字体（大小/颜色可设置）
HFONT  g_font      = nullptr;
HFONT  g_fontDone  = nullptr;
HFONT  g_fontRemindT = nullptr;  // 提醒弹窗正文（大号加粗）
HFONT  g_fontRemindB = nullptr;  // 提醒弹窗按钮（加粗）
HFONT  g_fontToastT  = nullptr;  // v1.12.1 气泡标题（加大加粗）
HFONT  g_fontToastB  = nullptr;  // v1.12.1 气泡正文（加大）
HBRUSH g_brWhite   = nullptr;
HBRUSH g_brBg      = nullptr;   // v0.27 窗口类背景刷（随主题重建）
HPEN   g_penCheck  = nullptr;   // v1.9.1：对勾几何笔（预创建复用）

WNDPROC g_oldEditProc = nullptr;
bool    g_imeComposing = false;   // 输入法正在组词（期间不动格式矩形；IME 自己管理插入点）
// v1.13.18：行内编辑框（任务文本）同样需要「组词期间冻结重排」与「插入点守恒」。
// 此前这两层保护只挂在底部添加框上，于是「编辑已有多行任务、补写『后天下午』」
// 时仍会被重排打断 —— 表现为第二段上屏后插入点被甩回组合串起点，文字插到
// 『后』字前面。行内编辑框的重排路径更长（SyncRowEditorSize ->
// RebuildRowTops -> RepositionRowEditor -> MoveWindow + EM_SETRECT），
// 被清掉的风险比添加框更高。
bool    g_rowImeComposing = false;   // 行内编辑框正在组词
// v1.13.10：主按钮悬停态（自绘按钮没有系统 hot 反馈，自己跟鼠标进出）
bool    g_btnAddHot    = false;
WNDPROC g_oldBtnAddProc = nullptr;
bool    g_btnMoreHot    = false;   // v1.13.11：右上角「⋯」悬停
WNDPROC g_oldBtnMoreProc = nullptr;

UINT   g_dpi        = 96;
RECT   g_listFrame  {};
