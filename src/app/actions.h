// 任务用例层：新增/完成/删除/批量/清空/导出。
// 改完数据后由本层触发视图刷新（RefreshList / ListRepaint）。
#pragma once

#include "common.h"

static const wchar_t kExportLabel[] = L"\u2193 导出";   // 分组标题右侧的导出入口

void ExportGroup(int grp);

bool HeaderExportHit(int grp, const RECT& ir, int x);

void DeleteTaskAt(int row);

void AddFromInput();

void ToggleTask(int row);

void MultiSetDone(const std::vector<int>& idxs, bool done);

void MultiDelete(const std::vector<int>& idxs);

void ClearDone();
