// 任务提醒：到点扫描、弹窗、完成。
#pragma once

#include "common.h"

extern bool g_remindOn;

extern int  g_remindMin;

void CheckReminders(); // 前置声明（设置对话框早于定义使用）
