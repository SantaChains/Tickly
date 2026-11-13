// 系统深色模式接入（uxtheme 未文档化接口，动态加载）
#include "common.h"
#include "ui/dark_mode.h"
#include "ui/context.h"
#include "ui/dwm_shell.h"
#include "ui/theme.h"

#include <uxtheme.h>   // SetWindowTheme

namespace {

// uxtheme 逆序导出（Win10 1809+）：
//   104 = AllowDarkModeForWindow(HWND, BOOL)
//   135 = SetPreferredAppMode(int)
//   136 = FlushMenuThemes()
using PfnAllowDarkModeForWindow = bool (WINAPI*)(HWND, bool);
using PfnSetPreferredAppMode   = int  (WINAPI*)(int);
using PfnFlushMenuThemes       = void (WINAPI*)();

enum : int {
    kAppModeDefault   = 0,
    kAppModeAllowDark = 1,
    kAppModeForceDark = 2,
    kAppModeForceLight = 3,
};

struct UxThemeApi {
    HMODULE                    mod = nullptr;
    PfnAllowDarkModeForWindow  allowWindow = nullptr;
    PfnSetPreferredAppMode     setMode = nullptr;
    PfnFlushMenuThemes         flushMenus = nullptr;

    UxThemeApi() {
        mod = LoadLibraryW(L"uxtheme.dll");
        if (!mod) return;
        allowWindow = (PfnAllowDarkModeForWindow)(void*)GetProcAddress(mod, MAKEINTRESOURCEA(104));
        setMode     = (PfnSetPreferredAppMode)(void*)GetProcAddress(mod, MAKEINTRESOURCEA(135));
        flushMenus  = (PfnFlushMenuThemes)(void*)GetProcAddress(mod, MAKEINTRESOURCEA(136));
    }
};

UxThemeApi& Ux() {
    static UxThemeApi api;
    return api;
}

// 系统控件的深色主题名。浅色时换回默认 "Explorer"，避免主题残留。
const wchar_t* ThemeNameForClass(const wchar_t* cls, bool dark) {
    static const wchar_t* kCfd[] = {
        L"Edit", L"ComboBox", L"ComboBoxEx32", L"msctls_hotkey32",
    };
    for (const wchar_t* c : kCfd)
        if (lstrcmpiW(cls, c) == 0) return dark ? L"DarkMode_CFD" : L"Explorer";
    if (lstrcmpiW(cls, L"SysDateTimePick32") == 0 ||
        lstrcmpiW(cls, L"ScrollBar") == 0 ||
        lstrcmpiW(cls, L"Button") == 0 ||
        lstrcmpiW(cls, L"Static") == 0)
        return dark ? L"DarkMode_Explorer" : L"Explorer";
    return nullptr;
}

BOOL CALLBACK ApplyToChild(HWND child, LPARAM lp) {
    const bool dark = lp != 0;
    wchar_t cls[64] {};
    if (!GetClassNameW(child, cls, 64)) return TRUE;
    const wchar_t* theme = ThemeNameForClass(cls, dark);
    if (!theme) return TRUE;
    if (Ux().allowWindow) Ux().allowWindow(child, dark);
    SetWindowTheme(child, theme, nullptr);
    InvalidateRect(child, nullptr, TRUE);
    return TRUE;
}

bool IsOwnTopLevel(HWND h) {
    wchar_t cls[64] {};
    if (!GetClassNameW(h, cls, 64)) return false;
    return lstrcmpiW(cls, L"TicklyClass") == 0 ||
           lstrcmpiW(cls, L"TicklyToast") == 0 ||
           lstrcmpiW(cls, L"#32770") == 0;      // 内存对话框模板
}

BOOL CALLBACK RefreshTopLevel(HWND h, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsOwnTopLevel(h)) return TRUE;
    wchar_t cls[64] {};
    if (GetClassNameW(h, cls, 64) && lstrcmpiW(cls, L"TicklyClass") == 0)
        return TRUE;   // 主窗口在上面的 TitleBar 分支处理过了
    DarkModeApplyToWindow(h);
    return TRUE;
}

}  // namespace

void DarkModeInit() {
    UxThemeApi& ux = Ux();
    if (ux.setMode)
        ux.setMode(g_themeDark ? kAppModeForceDark : kAppModeForceLight);
    if (ux.flushMenus) ux.flushMenus();
}

namespace {
void ApplyTo(HWND hwnd, bool themeChildren) {
    if (!hwnd || !IsWindow(hwnd)) return;
    const bool dark = g_themeDark != 0;
    if (Ux().allowWindow) Ux().allowWindow(hwnd, dark);
    DwmSetDarkTitleBar(hwnd, dark);
    if (!themeChildren) return;
    // 窗口自身的主题：影响滚动条、菜单与默认背景
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    EnumChildWindows(hwnd, ApplyToChild, dark ? 1 : 0);
    InvalidateRect(hwnd, nullptr, TRUE);
}
}  // namespace

void DarkModeApplyToWindow(HWND hwnd) { ApplyTo(hwnd, true); }

void DarkModeApplyTitleBar(HWND hwnd) { ApplyTo(hwnd, false); }

void DarkModeRefresh() {
    DarkModeInit();
    // 主窗口：只改标题栏。子控件全是自绘或 WM_CTLCOLOR，逐个 SetWindowTheme
    // 会加载 uxtheme 主题数据，实测多花 ~100ms 启动时间
    if (g_hwnd) DarkModeApplyTitleBar(g_hwnd);
    EnumWindows(RefreshTopLevel, 0);
}
