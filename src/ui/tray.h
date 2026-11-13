// 任务栏托盘：图标、菜单、后台运行。
#pragma once

#include "common.h"

enum { TRAY_PROMOTE_NONE    = 0,   // 未找到本程序的设置项（Win10 无此项 / 子键未建好）
       TRAY_PROMOTE_WROTE   = 1,   // 本次写入了 IsPromoted=1
       TRAY_PROMOTE_ALREADY = 2 }; // 本程序图标已是常驻状态

static const UINT WM_TRAY = WM_APP + 1;   // 托盘图标回调消息

extern UINT g_msgTaskbarCreated;

extern bool g_nidAdded;

extern bool g_trayPromoted;

extern int  g_promoteTries;

extern int  g_trayAddTries;

extern int  g_aliveMiss;

extern bool g_trayAliveCheck;

extern int  g_aliveOk;

extern bool g_trayRollback;

extern UINT g_hbInterval;

extern int  g_hbSteady;

void AddTrayIcon();

void EnsureTrayIcon(HWND hwnd);

void RemoveTrayIcon();

bool TrayIconAlive();

void BeginTrayAliveCheck();

void RollbackTrayPromotion();

void ShowMainWindow();

void HideToTray();

int PromoteTrayIcon();

void ApplyTrayPromotion(HWND hwnd);
