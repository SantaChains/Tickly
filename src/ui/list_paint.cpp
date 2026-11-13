// 自绘任务列表（几何 / 滚动 / 绘制 / 交互）（绘制实现）
#include "common.h"
#include "ui/list_view.h"
#include "app/actions.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "ui/row_editor.h"
#include "ui/context.h"
#include "core/task.h"
#include "ui/theme.h"
#include "core/time_util.h"


// ---------------------------- 绘制 ------------------------------------------
// 分组标题行（grp 0..4 完成 / 5..9 未完成）：v0.15 起两类分组均带折叠三角，
// 未完成分组无「↓ 导出」；空的未完成分组在 RebuildRows 中已隐藏不会到达这里
void DrawHeaderRow(HDC hdc, const RECT& rc, int grp) {
    bool open = GroupOpen(grp);
    bool unfinished = (grp >= 5);
    // v1.13.11：两类分组标题统一铺浅灰底带 —— 卡片内形成「小节标题」层次，
    // 未完成区原来是纯白，和任务行糊在一起分不出段
    FillSolid(hdc, rc, MixColor(COL_BG, COL_TEXT, 0.03));   // 略深于页面底
    RECT line = { rc.left, rc.top, rc.right, rc.top + Scale(1) };
    FillSolid(hdc, line, COL_BORDER);

    int tri = Scale(8);
    int tx = rc.left + Scale(14);
    int cy = rc.top + ((rc.bottom - rc.top) - tri) / 2;
    POINT pts[3];
    if (open) {
        pts[0] = { tx, cy };
        pts[1] = { tx + tri, cy };
        pts[2] = { tx + tri / 2, cy + tri };
    } else {
        pts[0] = { tx, cy };
        pts[1] = { tx, cy + tri };
        pts[2] = { tx + tri, cy + tri / 2 };
    }
    HBRUSH b = CachedBrush(COL_MUTED);   // v1.9.1：缓存复用，不再即建即删
    HPEN  p = CachedPen(COL_MUTED);
    HGDIOBJ ob = SelectObject(hdc, b);
    HGDIOBJ op = SelectObject(hdc, p);
    Polygon(hdc, pts, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);

    // v0.22：分组名称与任务数量分开绘制 —— 名称保持分组字体颜色，
    // 数量用浅灰（COL_GRP_CNT）弱化显示，不再与名称同色混排影响阅读
    const wchar_t* gname = (grp >= 5) ? OpenGroupName(grp) : GroupName(grp);
    int cnt = (grp >= 5) ? g_openCount[grp - 5] : g_grpCount[grp];
    wchar_t num[16];
    std::swprintf(num, 16, L"%d", cnt);
    HGDIOBJ of = SelectObject(hdc, unfinished ? g_fontGroupBold : g_fontGroupDone);
    SetBkMode(hdc, TRANSPARENT);
    RECT tr = { tx + tri + Scale(8), rc.top, rc.right - Scale(10), rc.bottom };
    // v1.13.6：未完成 / 已完成分组各自取自己的颜色（此前共用一套）
    SetTextColor(hdc, unfinished ? EffGrpCol() : EffDoneGrpCol());
    DrawTextW(hdc, gname, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SIZE ns {};
    GetTextExtentPoint32W(hdc, gname, (int)wcslen(gname), &ns);
    RECT nr = { tr.left + ns.cx + Scale(7), rc.top, tr.right, rc.bottom };
    if (unfinished) SelectObject(hdc, g_fontGroup);   // v0.25：计数保持常规字重
    SetTextColor(hdc, COL_GRP_CNT);
    DrawTextW(hdc, num, -1, &nr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, of);

    // v1.10.0 右侧「↓ 导出」：仅完成分组提供，且当分组内有任务时显示，
    // 单击保存为 txt（每任务一行）
    if (grp < 5 && g_grpCount[grp] > 0) {
        HGDIOBJ oe = SelectObject(hdc, g_fontGroupDone);   // v1.13.6：同已完成分组
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, EffDoneGrpCol());
        RECT er = { rc.left, rc.top, rc.right - Scale(10), rc.bottom };
        DrawTextW(hdc, kExportLabel, -1, &er,
                  DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, oe);
    }
}

// v1.13.11：已完成区总标题 —— 收纳五个子分组时唯一可见的一行，
// 整行可点，展开后五个分组重新出现，这一行继续作为总标题留在顶部
void DrawDoneSummaryRow(HDC hdc, const RECT& rc) {
    FillSolid(hdc, rc, MixColor(COL_BG, COL_TEXT, 0.03));
    RECT line = { rc.left, rc.top, rc.right, rc.top + Scale(1) };
    FillSolid(hdc, line, COL_BORDER);
    int tri = Scale(8);
    int tx  = rc.left + Scale(16);
    int cy  = rc.top + ((rc.bottom - rc.top) - tri) / 2;
    POINT pts[3];
    if (g_doneCollapsed) {                 // 收纳：三角向右
        pts[0] = { tx, cy };
        pts[1] = { tx, cy + tri };
        pts[2] = { tx + tri, cy + tri / 2 };
    } else {                               // 展开：三角向下
        pts[0] = { tx, cy };
        pts[1] = { tx + tri, cy };
        pts[2] = { tx + tri / 2, cy + tri };
    }
    HGDIOBJ ob = SelectObject(hdc, CachedBrush(COL_MUTED));
    HGDIOBJ op = SelectObject(hdc, CachedPen(COL_MUTED));
    Polygon(hdc, pts, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);

    HGDIOBJ of = SelectObject(hdc, g_fontGroupDone);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, EffDoneGrpCol());
    RECT tr = { tx + tri + Scale(8), rc.top, rc.right - Scale(16), rc.bottom };
    const wchar_t* label = L"已完成";
    DrawTextW(hdc, label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SIZE ns {};
    GetTextExtentPoint32W(hdc, label, (int)wcslen(label), &ns);
    wchar_t num[16];
    std::swprintf(num, 16, L"%d", CountDone());
    RECT nr = { tr.left + ns.cx + Scale(7), rc.top, tr.right, rc.bottom };
    SetTextColor(hdc, COL_GRP_CNT);
    DrawTextW(hdc, num, -1, &nr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, of);
}

void DrawTaskRow(HDC hdc, const RECT& rc, const Task& t, bool sel,
                        bool rowHover, bool hoverBox, bool divider, int idx) {
    // v0.28：选中与悬停同几何 —— 都是左右各内缩 2px 的圆角卡片
    //（RoundRect 半径 6px，空笔画纯底色）：选中 = 淡灰选中底、悬停 =
    // 淡灰悬停底（v0.27 悬停为整行铺色，与选中卡片的视觉语言不一致）；
    // 其余 = 白（深色主题下为内容卡色，随色板自动切换）
    if (sel || rowHover) {
        RECT cr = { rc.left + Scale(2), rc.top, rc.right - Scale(2), rc.bottom };
        HGDIOBJ op = SelectObject(hdc, GetStockObject(NULL_PEN));
        HGDIOBJ ob = SelectObject(hdc, CachedBrush(sel ? COL_SEL_BG
                                                       : COL_HOVER_BG));
        RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom,
                  Scale(6), Scale(6));
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
    } else {
        FillSolid(hdc, rc, COL_WHITE);
    }

    // 勾选框（悬停时高亮：未完成填充淡灰、边框变蓝；已完成边框加深）；
    // v0.27：直角 Rectangle 改为圆角（半径 5px 按 DPI 缩放），对勾不变
    int box = Scale(18);
    int bx  = rc.left + Scale(12);
    // v1.13.9：勾选框在整行内垂直居中，编辑态与展开态完全一致 —— 之前给
    // 编辑行单独做了「顶部对齐」，双击后勾选框会突然跳到行顶，看起来像
    // 换了另一个控件；编辑态本来就该是「展开态 + 一个光标」，其余不动
    int byy = rc.top + ((rc.bottom - rc.top) - box) / 2;
    RECT br = { bx, byy, bx + box, byy + box };
    COLORREF fc = t.done ? COL_ACCENT : (hoverBox ? COL_SEL_BG : COL_WHITE);
    COLORREF bc = t.done ? (hoverBox ? COL_HIGHLIGHT : COL_ACCENT)
                         : (hoverBox ? COL_ACCENT : COL_BOX_BD);
    HBRUSH fill = CachedBrush(fc);       // v1.9.1：缓存复用，不再即建即删
    HPEN   pen  = CachedPen(bc);
    HGDIOBJ of = SelectObject(hdc, fill);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, br.left, br.top, br.right, br.bottom, Scale(5), Scale(5));
    SelectObject(hdc, of);
    SelectObject(hdc, op);

    if (t.done) {
        HGDIOBJ oc = SelectObject(hdc, g_penCheck);   // v1.9.1：预创建复用
        MoveToEx(hdc, br.left + MulDiv(box, 24, 100),
                       br.top  + MulDiv(box, 53, 100), nullptr);
        POINT pts[2] = {
            { br.left + MulDiv(box, 42, 100), br.top + MulDiv(box, 71, 100) },
            { br.left + MulDiv(box, 77, 100), br.top + MulDiv(box, 31, 100) },
        };
        PolylineTo(hdc, pts, 2);
        SelectObject(hdc, oc);
    }

    SetBkMode(hdc, TRANSPARENT);

    // 右侧日期时间标签（无日期且未完成时为「＋ 日期」占位）
    bool od = false;
    if (t.due) FormatWhen(t.due, t.hasTime, t.done, &od);
    std::wstring when = WhenLabelText(t);
    int whenW = 0;
    if (!when.empty()) {
        HGDIOBJ ow = SelectObject(hdc, t.done ? g_fontSub : g_fontDate);   // v0.26 与绘制同源
        SIZE sz {};
        GetTextExtentPoint32W(hdc, when.c_str(), (int)when.size(), &sz);
        whenW = sz.cx;
        SelectObject(hdc, ow);
    }
    int textRight = rc.right - Scale(12) - RowGutter();
    if (whenW) textRight -= whenW + Scale(12);

    HGDIOBJ ot = SelectObject(hdc, t.done ? g_fontDone : g_font);
    SetTextColor(hdc, t.done ? COL_DONE : EffTaskCol());   // v1.13.0 可设置 + v0.27 随主题适配
    int textLeft = br.right + Scale(12);

    // v1.12.1：按折行结果分流 —— 无 \n 的长文本（如划词添加的一段话）
    // 折行后超过一行时同样走多行显示、随窗口宽度自动换行
    std::vector<std::wstring> lines = WrapTaskText(hdc, t.text,
                                                   textRight - textLeft);
    int lineH = TaskLineHeight(hdc);
    int total = (int)lines.size();
    if (total <= 1) {
        // 单行任务：单行 + 省略号显示。v0.27.1：不再用 DT_VCENTER 居中于
        // 整行 —— 改为与多行分支同机制：取规范行高 lineH，行矩形顶边
        // ty = 行顶 + (行高-lineH)/2，DrawText 顶对齐绘制。行内编辑框
        // （RowEditBoxRect 单行分支）窗口顶边取同一 ty，两态字形由同一
        // 绘制机制在同一矩形内完成，双击进入编辑不再有垂直跳动
        //（此前 DT_VCENTER 与 EDIT 顶部绘制的度量差导致文字上移，
        // 公式补偿跨字体/度量不可靠，故改为机制同源）
        int ty = rc.top + ((rc.bottom - rc.top) - lineH) / 2;
        if (ty < rc.top) ty = rc.top;
        RECT tr = { textLeft, ty, textRight, ty + lineH };
        DrawTextW(hdc, t.text.c_str(), -1, &tr,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        // 多行任务：自动换行显示全部文本；未展开最多 5 行，
        // 超出时末行省略号截断，右下角出现「展开 / 收起」按钮
        bool truncated = (!t.expanded && total > kTaskMaxLines);
        int vis = truncated ? kTaskMaxLines : total;
        // v0.28.1：编辑中的行不再绘制任务文本 —— 编辑框行数减少时（全选
        // 删除 / 逐行回删）行高与框高按编辑态公式立即回缩，任务文本却要
        // 等保存才更新，若仍按原文绘制，未被缩小后的框盖住的区域会露出
        // 旧文字（v0.23 只堵了封顶溢画，未封顶的收缩场景同样会漏；列表
        // 末行下方无后续行覆盖时最明显）。编辑框是不透明子窗口且底色与
        // 选中卡片一致，其下方的原文本从来不可见，跳过绘制对常态外观零
        // 影响；框外的日期标签仍由函数末尾绘制
        if (idx == g_rowEditIdx && g_rowEdit) vis = 0;
        // v0.17：编辑中的行不画「展开 / 收起」按钮（行高公式也不保留按钮行，
        // 编辑框覆盖全部内容）
        bool hasBtn = (idx != g_rowEditIdx) && total > kTaskMaxLines;
        // v1.13.2：文本起点固定为「行顶 + Scale(10)」，与 RowEditBoxRect 的
        // 编辑框顶边同一条公式。此前用垂直居中（(innerH - vis*lineH)/2），
        // 行高并入 4px 光标余量后居中结果变成 +12，与编辑框 +10 差 2px，
        // 双击时整段文字会往下跳 2px —— 固定顶边后两态完全同位。
        int top = rc.top + Scale(10);
        for (int i = 0; i < vis; ++i) {
            RECT lr = { textLeft, top + i * lineH,
                        textRight, top + (i + 1) * lineH };
            if (truncated && i == vis - 1) {
                // 末行截断：逐字回退到能容纳「…」为止
                std::wstring s = lines[(size_t)i];
                for (;;) {
                    std::wstring cand = s + L"…";
                    SIZE sz {};
                    GetTextExtentPoint32W(hdc, cand.c_str(),
                                          (int)cand.size(), &sz);
                    if (sz.cx <= textRight - textLeft || s.empty()) {
                        DrawTextW(hdc, cand.c_str(), -1, &lr,
                                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                        break;
                    }
                    s.pop_back();
                }
            } else {
                DrawTextW(hdc, lines[(size_t)i].c_str(), -1, &lr,
                          DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
        if (hasBtn) {
            // 「展开 / 收起」按钮（v0.25：颜色与分组标题保持一致；v0.27 同源）
            const wchar_t* label = t.expanded ? L"\u25B4 收起" : L"\u25BE 展开";
            SetTextColor(hdc, EffGrpCol());
            SIZE sz {};
            GetTextExtentPoint32W(hdc, label, (int)wcslen(label), &sz);
            // v1.13.5：按钮在行底右下角（原位置）；展开后的长任务行底可能
            // 在视口之外，此时吸附到视口底部 —— 否则「收起」根本点不到
            int by = rc.bottom - Scale(6) - Scale(17);
            bool floating = false;
            if (g_viewH > Scale(40) && by + Scale(17) > g_viewH - Scale(4)) {
                by = g_viewH - Scale(4) - Scale(17);
                floating = true;
            }
            if (by < rc.top + Scale(4)) by = rc.top + Scale(4);
            if (floating) {
                // v1.13.13：吸附时先铺一块不透明胶囊底 —— 否则按钮文字直接
                // 压在任务正文上（末行被视口裁切时最明显），按钮看不清、正文
                // 也被搅乱。胶囊比文字略大，点到边缘也算命中
                RECT bg = { textRight - sz.cx - Scale(8), by - Scale(4),
                            textRight + Scale(6), by + Scale(17) + Scale(4) };
                HGDIOBJ ob2 = SelectObject(hdc, CachedBrush(COL_WHITE));
                HGDIOBJ op2 = SelectObject(hdc, CachedPen(COL_BORDER));
                RoundRect(hdc, bg.left, bg.top, bg.right, bg.bottom,
                          Scale(9), Scale(9));
                SelectObject(hdc, ob2);
                SelectObject(hdc, op2);
            }
            RECT er = { textRight - sz.cx, by, textRight, by + Scale(17) };
            DrawTextW(hdc, label, -1, &er,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
    }
    SelectObject(hdc, ot);

    // 日期时间
    if (!when.empty()) {
        // v0.26：未完成任务（含逾期、今天、「＋ 日期」占位）的日期时间字体
        // 大小与颜色均可在设置中自定义（不再绑定分组标题颜色）；
        // 已完成保持固定小字号 + 完成灰
        HGDIOBJ ow = SelectObject(hdc, t.done ? g_fontSub : g_fontDate);
        SetTextColor(hdc, t.done ? COL_DONE : EffDtCol());   // v0.27 随主题适配
        // v1.13.9：日期同样保持整行垂直居中（编辑态与展开态一致，不跳动）
        RECT wr = { rc.right - Scale(12) - RowGutter() - whenW, rc.top,
                    rc.right - Scale(6) - RowGutter(), rc.bottom };
        DrawTextW(hdc, when.c_str(), -1, &wr,
                  DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(hdc, ow);
    }

    // v1.12.2 任务行之间的灰色分隔线：仅当下一行也是任务行时绘制
    //（与分组标题行顶线同色 COL_BORDER；v0.25 选中行为浅灰底、不再包边；
    // v0.28 悬停行与选中行同规则 —— 卡片行的底部分隔线一并隐藏）
    if (divider && !sel && !rowHover) {
        RECT sr = { rc.left, rc.bottom - Scale(1), rc.right, rc.bottom };
        FillSolid(hdc, sr, COL_BORDER);
    }
}
