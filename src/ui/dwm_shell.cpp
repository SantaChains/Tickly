// DWM 窗口外壳
#include "common.h"
#include "ui/dwm_shell.h"
#include "ui/context.h"
#include "ui/theme.h"


// ---------------------------- A 层：DWM 窗口外壳（v0.27） -------------------
// Win11 圆角 + 标题栏/边框颜色随主题 + 深色标题栏。
// 动态加载 dwmapi.dll 取 DwmSetWindowAttribute：不产生导入表依赖
//（导入表保持 8 个系统 DLL 不变），加载失败（老系统）静默降级为系统样式
typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
static PFN_DwmSetWindowAttribute s_pfnDwmSet = nullptr;
static bool s_dwmTried = false;
// 属性号与取值手工定义（不依赖 SDK 头文件版本）：
static const DWORD kDwmwaCornerPref = 33;    // DWMWA_WINDOW_CORNER_PREFERENCE
static const DWORD kDwmwaBorderColor = 34;   // DWMWA_BORDER_COLOR
static const DWORD kDwmwaCaptionColor = 35;  // DWMWA_CAPTION_COLOR
static const DWORD kDwmwaDarkMode = 20;      // DWMWA_USE_IMMERSIVE_DARK_MODE
static const DWORD kDwmwaDarkOld  = 19;      // 老内测版本号（20 失败时回退）
static const INT    kDwmcpRound = 2;         // DWMWCP_ROUND

static PFN_DwmSetWindowAttribute DwmSet() {
    if (!s_dwmTried) {
        s_dwmTried = true;
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        if (m) s_pfnDwmSet = (PFN_DwmSetWindowAttribute)
                             (void*)GetProcAddress(m, "DwmSetWindowAttribute");
    }
    return s_pfnDwmSet;
}

void DwmSetDarkTitleBar(HWND hwnd, bool dark) {
    const PFN_DwmSetWindowAttribute fn = DwmSet();
    if (!fn || !hwnd) return;
    INT v = dark ? 1 : 0;
    // 属性 20（Win10 19041+）；老内测版用 19
    if (fn(hwnd, kDwmwaDarkMode, &v, sizeof(v)) != S_OK)
        fn(hwnd, kDwmwaDarkOld, &v, sizeof(v));
}

void ApplyDwmTheme() {
    const PFN_DwmSetWindowAttribute fn = DwmSet();
    if (!fn || !g_hwnd) return;
    // Win11 圆角（Win10 无此属性：返回错误码，静默忽略）
    INT pref = kDwmcpRound;
    fn(g_hwnd, kDwmwaCornerPref, &pref, sizeof(pref));
    // 标题栏/边框颜色随主题（Win10 无效，静默降级为系统标题栏）
    COLORREF cap = COL_BG, border = COL_BORDER;
    fn(g_hwnd, kDwmwaCaptionColor, &cap, sizeof(cap));
    fn(g_hwnd, kDwmwaBorderColor, &border, sizeof(border));
    DwmSetDarkTitleBar(g_hwnd, g_themeDark != 0);
}
