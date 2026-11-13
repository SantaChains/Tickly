// 任务数据模型：内存表示，持久化格式见 app/store.cpp。
#pragma once

#include "common.h"


// ---------------------------- 数据模型 -------------------------------------
struct Task {
    std::wstring text;        // 任务标题（已剥离日期时间词）
    bool         done   = false;
    long long    doneAt = 0;  // 完成时间（epoch 秒，用于分组与倒序排列）
    long long    due    = 0;  // 截止时间（epoch 秒；仅日期时为当日 00:00）
    bool         hasTime = false; // due 是否含具体时刻
    bool         notified = false; // 已弹过提醒（每个任务只提醒一次）
    bool         expanded = false; // 多行任务展开状态（仅界面状态，不持久化）
};
