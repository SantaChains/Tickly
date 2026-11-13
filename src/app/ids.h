// 控件 ID 与自定义消息：所有 CreateWindow / SendMessage / 菜单命令共用。
#pragma once

#include "common.h"


// ---------------------------- 控件 ID --------------------------------------
static const int IDC_INPUT      = 1001;
static const int IDC_BTN_ADD    = 1002;
static const int IDC_LIST       = 1003;
// v1.13.12：底栏按钮已移除（完成靠勾选框、删除靠右键菜单）
static const int IDC_BTN_CLEAR  = 1006;   // 已并入右上角「⋯」菜单
static const int IDC_BTN_SETTINGS = 1007; // 已并入右上角「⋯」菜单
static const int IDC_BTN_MORE   = 1009;   // v1.13.11：右上角「更多」按钮

// 托盘/右键菜单命令 ID
static const int IDM_TRAY_SHOW = 3001;   // 显示主窗口
static const int IDM_TRAY_SET  = 3002;   // 设置
static const int IDM_TRAY_EXIT = 3003;   // 退出
static const int IDM_ROW_DELETE = 3004;  // 右键菜单：删除任务
static const int IDM_MULTI_DONE   = 3006; // v0.16 右键批量：完成所选
static const int IDM_MULTI_UNDONE = 3007; // v0.16 右键批量：还原所选
static const int IDM_MULTI_DELETE = 3008; // v0.16 右键批量：删除所选
static const int IDM_ROW_EDITDONE = 3005; // 右键菜单：修改完成日期（v0.15，已完成任务）
static const int IDM_APP_SETTINGS = 3011; // v1.13.11：右上角菜单 -> 设置
static const int IDM_APP_CLEAR    = 3012; // v1.13.11：右上角菜单 -> 清除已完成
static const int IDC_ROWEDIT    = 1008;  // 行内编辑框（列表子控件）
static const UINT WM_OPENDATE   = WM_APP + 2;  // 请求打开日期编辑对话框
static const UINT WM_ROWEDITCHK = WM_APP + 3;  // v0.22：编辑框首帧后复核行数
static const UINT WM_INPUTCHANGED = WM_APP + 4;  // v1.13.16：输入框内容变化的延迟处理

// 设置对话框：全局快捷键控件（v1.12.0）
static const int IDC_HK_SHOW    = 2105;  // 「显示主窗口」快捷键
static const int IDC_HK_CAPTURE = 2106;  // 「划词添加任务」快捷键
static const int IDC_REMIND_DONE = 2204; // v0.15 提醒弹窗「完成」按钮
