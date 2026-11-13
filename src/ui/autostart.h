// 开机自启（HKCU Run 键）。
#pragma once

#include "common.h"

bool GetAutoStart();

void SetAutoStart(bool on);   // 前置声明（GetAutoStart 里做旧值迁移）
