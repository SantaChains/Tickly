// 任务用例（增删改 / 批量 / 导出）
#include "common.h"
#include "app/actions.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "ui/input_box.h"
#include "ui/list_view.h"
#include "core/date_parse.h"
#include "ui/reminder.h"
#include "ui/row_editor.h"
#include "ui/context.h"
#include "app/store.h"
#include "core/task.h"
#include "core/text_util.h"
#include "ui/main_window.h"


// 导出分组内全部任务为 txt：每个任务一行（按完成时间倒序，与界面显示一致）
void ExportGroup(int grp) {
    std::vector<const Task*> items;
    for (const Task& t : g_tasks)
        if (t.done && InGroupDone(t.doneAt, grp)) items.push_back(&t);
    std::stable_sort(items.begin(), items.end(),
        [](const Task* a, const Task* b) { return a->doneAt > b->doneAt; });
    if (items.empty()) return;

    std::string out = "\xEF\xBB\xBF";    // UTF-8 BOM：记事本/Excel 打开不乱码
    for (const Task* t : items) {
        out += ToUtf8(t->text);
        out += "\r\n";
    }

    wchar_t file[MAX_PATH];
    std::swprintf(file, MAX_PATH, L"%ls.txt", GroupName(grp));
    static const wchar_t kFilter[] =
        L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    OPENFILENAMEW ofn {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return;               // 用户取消

    FILE* f = _wfopen(ofn.lpstrFile, L"wb");
    if (!f) {
        MessageBoxW(g_hwnd, L"文件写入失败，请检查保存位置是否可写。",
                    L"导出失败", MB_OK | MB_ICONERROR);
        return;
    }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    wchar_t okmsg[340];
    std::swprintf(okmsg, 340, L"已导出 %d 个任务到：\n%ls",
                  (int)items.size(), ofn.lpstrFile);
    MessageBoxW(g_hwnd, okmsg, L"导出完成", MB_OK | MB_ICONINFORMATION);
}

// 单击位置是否落在分组标题右侧的「↓ 导出」上（与 DrawHeaderRow 几何一致）
bool HeaderExportHit(int grp, const RECT& ir, int x) {
    if (grp < 0 || grp >= 5) return false;             // 仅完成分组提供导出
    if (g_grpCount[grp] <= 0) return false;            // 空分组不提供导出
    HDC hdc = GetDC(g_list);
    HGDIOBJ ow = SelectObject(hdc, g_fontGroup);   // v1.13.0：与分组标题同字体
    SIZE sz {};
    GetTextExtentPoint32W(hdc, kExportLabel, (int)wcslen(kExportLabel), &sz);
    SelectObject(hdc, ow);
    ReleaseDC(g_list, hdc);
    int right = ir.right - Scale(10);
    int left  = right - sz.cx - Scale(8);
    return x >= left && x <= right;
}

// 删除指定显示行对应的任务（右键菜单用，待办/已完成均可）
void DeleteTaskAt(int row) {
    if (row < 0 || row >= (int)g_rows.size() || g_rows[row] < 0) return;
    int idx = g_rows[row];
    if (g_rowEditRow == row) CloseRowEditor(false);  // 正在编辑则先关编辑框
    g_tasks.erase(g_tasks.begin() + idx);
    SaveTasks();
    RebuildRows();
    UpdateButtons();
}

void AddFromInput() {
    int n = GetWindowTextLengthW(g_input);
    std::wstring raw;
    raw.resize((size_t)n + 1);
    GetWindowTextW(g_input, &raw[0], n + 1);
    raw.resize(wcslen(raw.c_str()));
    std::wstring text = NormalizeMultiline(raw);   // v1.12.0：保留换行结构
    if (text.empty()) {
        MessageBeep(MB_ICONWARNING);
        SetFocus(g_input);
        return;
    }
    Task t;
    if (text.find(L'\n') == std::wstring::npos) {
        WhenResult w = ParseWhen(text);
        if (w.ok && !w.stripped.empty()) {
            t.text    = w.stripped;
            t.due     = w.due;
            t.hasTime = w.hasTime;
        } else {
            t.text = text;   // 日期词占满整行（如只输入“明天”）-> 按普通任务处理
        }
    } else {
        t.text = text;       // v1.12.0 多行文本：整段即任务，不做日期解析
    }
    g_tasks.insert(g_tasks.begin(), t);   // 新任务置顶
    g_multiSel.clear();                   // v0.16：下标整体 +1，多选失效
    SaveTasks();
    RebuildRows();
    g_selRow = -1;                        // 默认不选中
    g_anchorRow = -1;                     // v0.16：锚点一并重置
    SetWindowTextW(g_input, L"");
    SetFocus(g_input);
    UpdateButtons();
    UpdateInputSize();                    // 输入框回落到单行高度
    CheckReminders();                     // 新任务若已进入提醒窗口则立即提示
}

void ToggleTask(int row) {
    if (row < 0 || row >= (int)g_rows.size()) return;
    int idx = g_rows[row];
    if (idx == ROW_FILL) return;             // v1.10.0 填塞行无操作
    if (idx < 0) { ToggleGroup(-1 - idx); return; }
    g_tasks[(size_t)idx].done = !g_tasks[(size_t)idx].done;
    if (g_tasks[(size_t)idx].done) {
        g_tasks[(size_t)idx].doneAt = (long long)time(nullptr);
    } else {
        // 取消完成 -> 移回待办区顶部
        Task t = g_tasks[(size_t)idx];
        t.done = false;
        t.doneAt = 0;
        t.notified = false;   // 重新进入提醒流程
        g_tasks.erase(g_tasks.begin() + idx);
        g_tasks.insert(g_tasks.begin(), t);
        g_multiSel.clear();               // v0.16：下标整体 +1，多选失效
        RebuildRows();
        g_selRow = 0;
        ListEnsureVisible(0);
        SaveTasks();
        UpdateButtons();
        return;
    }
    SaveTasks();
    RebuildRows();
    // 选中它在新分组中的行
    g_selRow = RowOfIdx(idx);
    if (g_selRow >= 0) ListEnsureVisible(g_selRow);
    ListRepaint();
    UpdateButtons();
}

// v0.16：批量完成 / 还原所选任务（还原的任务移回待办区顶部，保持原相对顺序）
void MultiSetDone(const std::vector<int>& idxs, bool done) {
    if (idxs.empty()) return;
    std::vector<int> ids = idxs;
    std::sort(ids.begin(), ids.end());
    if (!done) {
        std::vector<Task> moved;
        for (int i : ids) {
            if (i < 0 || i >= (int)g_tasks.size()) continue;
            Task t = g_tasks[(size_t)i];
            t.done     = false;
            t.doneAt   = 0;
            t.notified = false;              // 重新进入提醒流程
            moved.push_back(t);
        }
        for (size_t k = ids.size(); k > 0; --k) {
            int i = ids[k - 1];
            if (i >= 0 && i < (int)g_tasks.size())
                g_tasks.erase(g_tasks.begin() + i);
        }
        for (size_t k = moved.size(); k > 0; --k)
            g_tasks.insert(g_tasks.begin(), moved[k - 1]);
    } else {
        long long now = (long long)time(nullptr);
        for (int i : ids)
            if (i >= 0 && i < (int)g_tasks.size()) {
                g_tasks[(size_t)i].done   = true;
                g_tasks[(size_t)i].doneAt = now;
            }
    }
    g_multiSel.clear();
    g_selRow    = -1;
    g_anchorRow = -1;
    SaveTasks();
    RebuildRows();
    UpdateButtons();
    ListRepaint();
}

// v0.16：批量删除所选任务
void MultiDelete(const std::vector<int>& idxs) {
    if (idxs.empty()) return;
    std::vector<int> ids = idxs;
    std::sort(ids.begin(), ids.end(), [](int a, int b) { return a > b; });
    for (int i : ids)
        if (i >= 0 && i < (int)g_tasks.size())
            g_tasks.erase(g_tasks.begin() + i);
    g_multiSel.clear();
    g_selRow    = -1;
    g_anchorRow = -1;
    SaveTasks();
    RebuildRows();
    UpdateButtons();
    ListRepaint();
}

// v2.3.2：删除所选（Delete 键与右键菜单共用；多选时批量删除）
void DeleteSelected() {
    if (!g_multiSel.empty()) {               // v0.16：多选时批量删除
        MultiDelete(g_multiSel);
        return;
    }
    if (g_selRow < 0 || g_selRow >= (int)g_rows.size() || g_rows[g_selRow] < 0) return;
    int idx = g_rows[g_selRow];
    if (g_rowEditRow == g_selRow) CloseRowEditor(false);   // 正在编辑则先关编辑框
    g_tasks.erase(g_tasks.begin() + idx);
    g_anchorRow = -1;
    SaveTasks();
    RebuildRows();
    UpdateButtons();
}

void ClearDone() {
    for (int i = (int)g_tasks.size() - 1; i >= 0; --i) {
        if (g_tasks[(size_t)i].done) g_tasks.erase(g_tasks.begin() + i);
    }
    SaveTasks();
    RebuildRows();
    UpdateButtons();
}
