// 全局快捷键
#include "common.h"
#include "ui/hotkeys.h"
#include "ui/context.h"


// v1.12.0 全局快捷键（持久化）：mod = MOD_ALT/MOD_CONTROL/MOD_SHIFT 位或，
// vk = 虚拟键码；默认「显示主窗口」Alt+Q、「划词添加任务」Alt+1
UINT g_hkShowMod = MOD_ALT;
UINT g_hkShowVk  = 'Q';
UINT g_hkCapMod  = MOD_ALT;
UINT g_hkCapVk   = '1';
static bool g_hkShowOk  = false;      // 本运行期注册结果（设置对话框展示）
static bool g_hkCapOk   = false;

// 主键展示名（不含修饰键）
std::wstring HotkeyKeyName(UINT vk) {
    wchar_t key[32] = L"";
    if ((vk >= L'0' && vk <= L'9') || (vk >= L'A' && vk <= L'Z')) {
        key[0] = (wchar_t)vk; key[1] = 0;
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        std::swprintf(key, 32, L"F%d", (int)(vk - VK_F1 + 1));
    } else if (vk) {
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        if (sc) GetKeyNameTextW((LONG)(sc << 16), key, 32);
    }
    return key;
}

// 快捷键展示名：如「Alt+Q」「Ctrl+Shift+F1」
static std::wstring HotkeyName(UINT mod, UINT vk) {
    static const struct { UINT m; const wchar_t* n; } km[] = {
        { MOD_CONTROL, L"Ctrl+" }, { MOD_ALT, L"Alt+" },
        { MOD_SHIFT, L"Shift+" },  { MOD_WIN, L"Win+" },
    };
    std::wstring s;
    for (const auto& k : km)
        if (mod & k.m) s += k.n;
    const std::wstring key = HotkeyKeyName(vk);
    if (key.empty()) return std::wstring();
    return s + key;
}

static bool RegisterOneHotkey(UINT id, UINT mod, UINT vk) {
    if (!g_hwnd || !vk || !mod) return false;
    return RegisterHotKey(g_hwnd, (int)id, mod | MOD_NOREPEAT, vk) != FALSE;
}

// 注册/重注册两个全局快捷键（启动、设置保存后调用）；结果记录在 g_hkShowOk/CapOk
void ApplyHotkeys() {
    if (!g_hwnd) return;
    UnregisterHotKey(g_hwnd, HK_ID_SHOW);
    UnregisterHotKey(g_hwnd, HK_ID_CAP);
    g_hkShowOk = RegisterOneHotkey(HK_ID_SHOW, g_hkShowMod, g_hkShowVk);
    g_hkCapOk  = RegisterOneHotkey(HK_ID_CAP,  g_hkCapMod,  g_hkCapVk);
}

// 设置对话框里的快捷键生效状态行
std::wstring HotkeyStatusText() {
    std::wstring a = HotkeyName(g_hkShowMod, g_hkShowVk);
    std::wstring b = HotkeyName(g_hkCapMod,  g_hkCapVk);
    if (a.empty()) a = L"未设置";
    if (b.empty()) b = L"未设置";
    return L"快捷键：显示 " + a + (g_hkShowOk ? L"（已生效）" : L"（被占用）")
         + L" · 划词 "  + b + (g_hkCapOk  ? L"（已生效）" : L"（被占用）");
}
