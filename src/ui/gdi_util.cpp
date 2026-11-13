// GDI 工具与资源
#include "common.h"
#include "ui/gdi_util.h"
#include "ui/context.h"
#include "ui/theme.h"


// ---------------------------- 小工具 ----------------------------------------
int Scale(int v) { return MulDiv(v, (int)g_dpi, 96); }

// v0.15：任务行右侧为悬浮滚动条预留的空隙 —— 滚动条 thumb 距右缘 4~11px，
// 日期 /「＋ 日期」标签整体左移让位，不再被滚动条盖住。
// 行高计算、绘制、命中测试、行内编辑几何全部使用同一函数，保证一致
int RowGutter() { return Scale(10); }

// v1.9.1 资源优化：GDI 对象缓存。
// 界面用到的颜色总数有限，预创建后按颜色复用，不再每次绘制都反复
// 创建/销毁内核 GDI 对象。GDI 句柄为进程级资源，UI 单线程使用无并发
// 问题，进程退出自动回收。
// v0.27：缓存存储移到文件作用域 —— 主题切换时需要整体失效重建
//（两套色板 + 按钮按下态派生色的并集可能超过旧 12 槽，槽位满后新颜色
// 拿不到句柄会画不出；扩到 16 留余量）
static COLORREF s_brKey[16];  static HBRUSH s_brVal[16];  static int s_brN  = 0;
static COLORREF s_penKey[16]; static HPEN   s_penVal[16]; static int s_penN = 0;

void InvalidateGdiCaches() {
    for (int i = 0; i < s_brN; ++i)
        if (s_brVal[i]) DeleteObject(s_brVal[i]);
    s_brN = 0;
    for (int i = 0; i < s_penN; ++i)
        if (s_penVal[i]) DeleteObject(s_penVal[i]);
    s_penN = 0;
}

HBRUSH CachedBrush(COLORREF c) {
    for (int i = 0; i < s_brN; ++i)
        if (s_brKey[i] == c) return s_brVal[i];
    HBRUSH b = CreateSolidBrush(c);
    if (!b) return nullptr;
    if (s_brN < 16) { s_brKey[s_brN] = c; s_brVal[s_brN] = b; ++s_brN; }
    else            DeleteObject(b);          // 不会发生：颜色集合 < 16
    return b;
}

HPEN CachedPen(COLORREF c) {
    for (int i = 0; i < s_penN; ++i)
        if (s_penKey[i] == c) return s_penVal[i];
    HPEN p = CreatePen(PS_SOLID, 1, c);
    if (!p) return nullptr;
    if (s_penN < 16) { s_penKey[s_penN] = c; s_penVal[s_penN] = p; ++s_penN; }
    else            DeleteObject(p);          // 不会发生：颜色集合 < 16
    return p;
}

void FillSolid(HDC hdc, const RECT& rc, COLORREF c) {
    FillRect(hdc, &rc, CachedBrush(c));
}

HFONT MakeFont(int pxHeight, bool bold , bool strike ) {
    return CreateFontW(-Scale(pxHeight), 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
                       FALSE, FALSE, strike ? TRUE : FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

static HFONT MakeFontBold(int pxHeight) {   // 真加粗（FW_BOLD），提醒弹窗用
    return CreateFontW(-Scale(pxHeight), 0, 0, 0, FW_BOLD,
                       FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

// v2.0：把「尺寸随 DPI 变化」的 GDI 资源集中到一处创建/重建。
// 启动时调一次；收到 WM_DPICHANGED（拖到不同缩放比的显示器、系统缩放改了）
// 时再调一次，字体按新 DPI 重新建，文字才不会发虚。
// 注意：任务/分组/日期字体由 ApplyFontSettings() 按用户设置重建，不在这里。
void CreateUiResources() {
    HFONT nf;
    nf = MakeFont(20, true);        // 标题
    if (g_fontTitle) DeleteObject(g_fontTitle);
    g_fontTitle = nf;
    nf = MakeFont(12);              // 副标题
    if (g_fontSub) DeleteObject(g_fontSub);
    g_fontSub = nf;
    nf = MakeFontBold(17);          // 提醒弹窗正文
    if (g_fontRemindT) DeleteObject(g_fontRemindT);
    g_fontRemindT = nf;
    nf = MakeFontBold(13);          // 提醒弹窗按钮
    if (g_fontRemindB) DeleteObject(g_fontRemindB);
    g_fontRemindB = nf;
    nf = MakeFontBold(15);          // v1.12.1：气泡标题（加大加粗）
    if (g_fontToastT) DeleteObject(g_fontToastT);
    g_fontToastT = nf;
    nf = MakeFont(14);              // v1.12.1：气泡正文（加大）
    if (g_fontToastB) DeleteObject(g_fontToastB);
    g_fontToastB = nf;
    // 对勾几何笔：宽度随 DPI（Scale(2)），颜色固定白色
    if (g_penCheck) DeleteObject(g_penCheck);
    LOGBRUSH lb { BS_SOLID, COL_WHITE, 0 };
    g_penCheck = ExtCreatePen(PS_GEOMETRIC | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
                              Scale(2), &lb, 0, nullptr);
}
