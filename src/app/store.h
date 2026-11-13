// 持久化：tasks.txt 读写。
// 任务数据与全部设置项都在这一个文件里落盘（唯一的序列化点）。
#pragma once

#include "common.h"
#include "core/task.h"

extern std::vector<Task> g_tasks;

void SaveTasks();

void LoadTasks();
