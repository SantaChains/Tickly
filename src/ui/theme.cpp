// 外观主题（配色 / 字体设置 / DWM 属性）
#include "common.h"
#include "ui/theme.h"
#include "ui/dark_mode.h"
#include "ui/dwm_shell.h"
#include "ui/gdi_util.h"
#include "app/rows.h"
#include "ui/input_box.h"
#include "ui/context.h"
#include "ui/main_window.h"


// v1.13.0 字体外观设置（持久化，设置对话框可改）：
int      g_taskFontSize = 14;                 // 任务内容字体大小（px）
COLORREF g_taskFontCol  = RGB(29, 29, 31);    // 任务内容字体颜色
int      g_grpFontSize  = 12;                 // 未完成分组标题字号（px）
COLORREF g_grpFontCol   = RGB(110, 110, 115); // 未完成分组标题颜色
// v1.13.6：已完成分组（本周/上周/本月/上月/其他）单独一套字号与颜色
int      g_doneGrpFontSize = 12;
COLORREF g_doneGrpFontCol  = RGB(110, 110, 115);
int      g_dtFontSize   = 12;                 // v0.26 日期时间字体大小（px）
COLORREF g_dtFontCol    = RGB(110, 110, 115); // v0.26 日期时间字体颜色（默认与分组标题一致）

// ---------------------------- 配色 ------------------------------------------
// v0.27 主题引擎：COL_* 由编译期常量改为运行时变量，Light/Dark 两套常量
// 色板表（kPalLight/kPalDark）由 ApplyTheme() 按当前模式整表拷贝。
// Light 色板与 v0.26 逐值一致（浅色模式除 C 层四项视觉外零变化）：
// 强调色 #007AFF、深蓝 #0062CC、错误红 #FF3B30、正文 #1D1D1F、
// 次要文字 #6E6E73、分隔线 #E5E5EA、背景 #F2F2F7、选中浅灰 #EEEEEE；
// Dark 色板为 Apple 深色风（BG #1C1C1E、内容卡 #2C2C2E、强调 #0A84FF…）
COLORREF COL_ACCENT   = RGB(0, 122, 255);    // Apple 蓝 #007AFF
COLORREF COL_HIGHLIGHT= RGB(0, 98, 204);     // 深蓝（今天/识别提示）
COLORREF COL_OVERDUE  = RGB(255, 59, 48);    // Apple 红 #FF3B30
COLORREF COL_TEXT     = RGB(29, 29, 31);     // Apple label #1D1D1F
COLORREF COL_MUTED    = RGB(110, 110, 115);  // secondary #6E6E73
COLORREF COL_DONE     = RGB(174, 174, 178);  // 运行时派生（见 ApplyTheme）
COLORREF COL_GRP_CNT  = RGB(174, 174, 178);  // v0.22 分组计数弱化灰（#AEAEB2）
COLORREF COL_BORDER   = RGB(229, 229, 234);  // separator #E5E5EA
COLORREF COL_BOX_BD   = RGB(209, 209, 214);  // #D1D1D6
COLORREF COL_BG       = RGB(242, 242, 247);  // systemGray6 #F2F2F7
COLORREF COL_SEL_BG   = RGB(238, 238, 238);  // v0.26 选中浅灰底 #EEEEEE
COLORREF COL_WHITE    = RGB(255, 255, 255);  // 内容卡面 #FFFFFF（深色 #2C2C2E）
COLORREF COL_SB       = RGB(193, 198, 206);  // v1.13.0 滚动条
COLORREF COL_SB_HOT   = RGB(157, 163, 172);  // 滚动条悬停/拖动
COLORREF COL_HOVER_BG = RGB(242, 242, 247);  // v0.27 整行悬停底色（随主题）

// ---------------------------- 主题引擎（v0.27 B 层） ------------------------
// 三态模式：0 = 浅色，1 = 深色，2 = 跟随系统（默认）；g_themeDark 为解析后
// 实际生效的深浅（跟随系统时读注册表 AppsUseLightTheme，0 = 系统深色）
int  g_themeMode = 2;
int  g_themeDark = 0;
// 用户自定义字体颜色的浅色板默认值（v1.13.0/v0.26 时代的初始值）：
static const COLORREF kDefTaskCol = RGB(29, 29, 31);
static const COLORREF kDefGrpCol  = RGB(110, 110, 115);
static const COLORREF kDefDtCol   = RGB(110, 110, 115);

struct ThemePalette {
    COLORREF accent, highlight, overdue, text, muted, grpCnt,
             border, boxBd, bg, selBg, card, sb, sbHot, hoverBg;
};
// Light：与 v0.26 各常量逐值一致
static const ThemePalette kPalLight = {
    RGB(0, 122, 255),    // accent    #007AFF
    RGB(0, 98, 204),     // highlight #0062CC
    RGB(255, 59, 48),    // overdue   #FF3B30
    RGB(29, 29, 31),     // text      #1D1D1F
    RGB(110, 110, 115),  // muted     #6E6E73
    RGB(174, 174, 178),  // grpCnt    #AEAEB2
    RGB(229, 229, 234),  // border    #E5E5EA
    RGB(209, 209, 214),  // boxBd     #D1D1D6
    RGB(242, 242, 247),  // bg        #F2F2F7
    RGB(238, 238, 238),  // selBg     #EEEEEE（v0.26 选中色）
    RGB(255, 255, 255),  // card      #FFFFFF
    RGB(193, 198, 206),  // sb        #C1C6CE
    RGB(157, 163, 172),  // sbHot     #9DA3AC
    RGB(242, 242, 247),  // hoverBg   #F2F2F7（白底行上的淡灰悬停）
};
// Dark：Apple 深色风
static const ThemePalette kPalDark = {
    RGB(10, 132, 255),   // accent    #0A84FF
    RGB(64, 156, 255),   // highlight #409CFF
    RGB(255, 69, 58),    // overdue   #FF453A
    RGB(242, 242, 247),  // text      #F2F2F7
    RGB(152, 152, 159),  // muted     #98989F
    RGB(99, 99, 102),    // grpCnt    #636366
    RGB(58, 58, 60),     // border    #3A3A3C
    RGB(72, 72, 74),     // boxBd     #48484A
    RGB(28, 28, 30),     // bg        #1C1C1E
    RGB(58, 58, 60),     // selBg     #3A3A3C
    RGB(44, 44, 46),     // card      #2C2C2E
    RGB(90, 90, 94),     // sb        #5A5A5E
    RGB(124, 124, 130),  // sbHot     #7C7C82
    RGB(51, 51, 54),     // hoverBg   #333336
};

// 线性混色：Mix(a, b, t) = a*(1-t) + b*t（四舍五入）
COLORREF MixColor(COLORREF a, COLORREF b, double t) {
    int r = (int)(GetRValue(a) * (1.0 - t) + GetRValue(b) * t + 0.5);
    int g = (int)(GetGValue(a) * (1.0 - t) + GetGValue(b) * t + 0.5);
    int bl = (int)(GetBValue(a) * (1.0 - t) + GetBValue(b) * t + 0.5);
    return RGB(r, g, bl);
}

// 按比例加深（按钮按下态轻微加深用，pct = 加深百分比 0~100）
COLORREF DarkenColor(COLORREF c, int pct) {
    int r = GetRValue(c) * (100 - pct) / 100;
    int g = GetGValue(c) * (100 - pct) / 100;
    int b = GetBValue(c) * (100 - pct) / 100;
    return RGB(r, g, b);
}

// 读系统「应用模式」深浅：注册表 HKCU\...\Themes\Personalize 的
// AppsUseLightTheme（DWORD，0 = 深色）；读不到按浅色
static bool SysPrefDark() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes"
                      L"\\Personalize",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD v = 1, sz = sizeof(v), t = REG_DWORD;
        LSTATUS st = RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, &t,
                                      (LPBYTE)&v, &sz);
        RegCloseKey(k);
        if (st == ERROR_SUCCESS && t == REG_DWORD) return v == 0;
    }
    return false;
}

// 解析三态模式 -> 实际深浅
static void ResolveThemeMode() {
    g_themeDark = (g_themeMode == 1) ||
                  (g_themeMode == 2 && SysPrefDark());
}

// v0.27：用户自定义字体颜色的主题适配 —— 用户从未改过色（仍等于浅色板
// 默认值）时返回当前主题对应位色板值，否则保持用户颜色。
// 所有绘制与测量点必须统一走这三个辅助函数（同文字多测量点必须同源）
COLORREF EffTaskCol() {
    return (g_taskFontCol == kDefTaskCol) ? COL_TEXT   : g_taskFontCol;
}
COLORREF EffDoneGrpCol() {
    return (g_doneGrpFontCol == kDefGrpCol) ? COL_MUTED : g_doneGrpFontCol;
}

COLORREF EffGrpCol() {
    return (g_grpFontCol  == kDefGrpCol ) ? COL_MUTED  : g_grpFontCol;
}
COLORREF EffDtCol() {
    return (g_dtFontCol   == kDefDtCol  ) ? COL_MUTED  : g_dtFontCol;
}

// v0.27 B 层：主题应用。职责清单：拷贝色板 → 派生色 → 重建全部缓存
// 画刷/画笔 → 重建窗口类背景刷 → 重设 DWM 属性 → 全窗口重绘。
// 主题切换与窗口创建（WM_CREATE）时都会调用
void ApplyTheme() {
    ResolveThemeMode();
    const ThemePalette& p = g_themeDark ? kPalDark : kPalLight;
    COL_ACCENT    = p.accent;
    COL_HIGHLIGHT = p.highlight;
    COL_OVERDUE   = p.overdue;
    COL_TEXT      = p.text;
    COL_MUTED     = p.muted;
    COL_GRP_CNT   = p.grpCnt;
    COL_BORDER    = p.border;
    COL_BOX_BD    = p.boxBd;
    COL_BG        = p.bg;
    COL_SEL_BG    = p.selBg;
    COL_WHITE     = p.card;
    COL_SB        = p.sb;
    COL_SB_HOT    = p.sbHot;
    COL_HOVER_BG  = p.hoverBg;
    // 派生色：已完成灰 = Mix(正文, 背景, 0.68)（预混伪透明）。
    // 浅色下 ≈ #AEAEB2 与 v0.26 常量一致；深色下 ≈ #606066
    COL_DONE = MixColor(COL_TEXT, COL_BG, 0.68);
    // 重建缓存画刷/画笔与窗口类背景刷（先建新刷并接管类背景，再删旧刷，
    // 避免 DeleteObject 后类仍引用已删除句柄的窗口期）
    InvalidateGdiCaches();
    HBRUSH nb = CreateSolidBrush(COL_BG);
    if (g_hwnd && nb)
        SetClassLongPtrW(g_hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)nb);
    if (g_brBg) DeleteObject(g_brBg);
    g_brBg = nb;
    // DWM 外壳属性随主题重设
    ApplyDwmTheme();
    // 系统控件（菜单/标题栏/编辑框）跟随主题：进程首选外观 + 已打开窗口重刷
    DarkModeInit();
    DarkModeRefresh();
    // 全窗口重绘（主窗口 + 列表 + 输入框；行内编辑框随列表 WM_PAINT 重建）
    if (g_hwnd)  InvalidateRect(g_hwnd, nullptr, TRUE);
    if (g_list)  InvalidateRect(g_list, nullptr, FALSE);
    if (g_input) InvalidateRect(g_input, nullptr, TRUE);
    // v2.1：切外观后强制对齐一次输入框格式矩形。切主题会改 DWM 边框/标题栏
    // 属性，可能连带触发 WM_SIZE 重排；重排后 EDIT 的居中矩形若没重设，
    // 单行文字与光标会偏上。这里无视缓存强制重设一遍（代价是一次 EM_SETRECT）
    if (g_input) CenterInput(true);
}
void ApplyFontSettings() {
    HFONT nf;
    nf = MakeFont(g_taskFontSize);
    if (g_font) DeleteObject(g_font);
    g_font = nf;
    nf = MakeFont(g_taskFontSize, false, true);
    if (g_fontDone) DeleteObject(g_fontDone);
    g_fontDone = nf;
    nf = MakeFont(g_grpFontSize);
    if (g_fontGroup) DeleteObject(g_fontGroup);
    g_fontGroup = nf;
    nf = MakeFont(g_grpFontSize, true);  // v0.25：未完成分组标题加粗
    if (g_fontGroupBold) DeleteObject(g_fontGroupBold);
    g_fontGroupBold = nf;
    nf = MakeFont(g_doneGrpFontSize);    // v1.13.6：已完成分组独立字体
    if (g_fontGroupDone) DeleteObject(g_fontGroupDone);
    g_fontGroupDone = nf;
    nf = MakeFont(g_dtFontSize);         // v0.26：日期时间字体大小可设置
    if (g_fontDate) DeleteObject(g_fontDate);
    g_fontDate = nf;
    if (g_input) {
        SendMessageW(g_input, WM_SETFONT, (WPARAM)g_font, TRUE);
        // v2.1：WM_SETFONT 会把 EM_SETRECT 设过的格式矩形清成整块客户区，
        // 缓存必须失效，否则下面 Layout/CenterInput 会误判「没变化」跳过重设，
        // 表现为「设置里保存后，顶部输入框的光标靠上、不居中」
        InvalidateInputFmtRect();
    }
    RebuildRows();                       // 行高/折行随字号变化，重建前缀和
    Layout();
    CenterInput();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
}
