// 时间工具：epoch 与本地时间互转、周/月边界、日期文案。
#pragma once

#include "common.h"
#include "core/task.h"

tm LocalTmOf(long long ep);

long long MakeEpoch(int y, int mo, int d, int h, int mi);

int DaysInMonth(int y, int m);

long long TodayMidnight();

long long ThisWeekStart();

long long ThisMonthStart();

long long LastWeekStart();

long long LastMonthStart();

std::wstring FormatWhen(long long due, bool hasTime, bool done,
                               bool* overdueOut);

std::wstring WhenLabelText(const Task& t);
