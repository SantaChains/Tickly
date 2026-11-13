// 任务栏托盘
#include "common.h"
#include "ui/tray.h"
#include "ui/context.h"

UINT g_msgTaskbarCreated = 0;      // Explorer 重启后恢复图标
bool g_nidAdded       = false;     // 托盘图标已添加
static bool g_trayTipShown   = false;     // 首次最小化气泡已显示
bool g_trayPromoted   = false;     // 已把图标设为常驻（IsPromoted=1）
int  g_promoteTries   = 0;         // NotifyIconSettings 子键异步建立的重试计数
int  g_trayAddTries   = 0;         // NIM_ADD 失败重试计数
// v1.9.0 托盘心跳保活：图标可以被折叠，但绝不能消失
int  g_aliveMiss      = 0;         // 心跳探测连续失败计数（2 次才判定丢失）
bool g_trayAliveCheck = false;     // 提升后验证期：确认 IsPromoted 无副作用
int  g_aliveOk        = 0;         // 验证期内连续存活轮数（满 2 轮通过）
bool g_trayRollback   = false;     // 已回滚为系统默认（本运行期不再提升）
// v1.9.1 心跳自适应频率：验证期/异常期 5 秒快速收敛，稳定后 30 秒低频兜底
UINT g_hbInterval     = 5000;      // 当前心跳间隔（毫秒）
int  g_hbSteady       = 0;         // 连续正常轮数（满 6 轮降为低频）

// ---------------------------- 任务栏托盘 -------------------------------------
void AddTrayIcon() {
    if (!g_hwnd) return;
    NOTIFYICONDATAW nid {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hwnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                                  IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!nid.hIcon)
        nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                      MAKEINTRESOURCEW(1), IMAGE_ICON,
                                      16, 16, LR_DEFAULTCOLOR);
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wchar_t tip[128] = L"";
    GetWindowTextW(g_hwnd, tip, 128);
    lstrcpynW(nid.szTip, tip, 128);
    g_nidAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

// NIM_ADD 偶发失败（任务栏未就绪等）：定时重试最多 5 次 x 0.5 秒
void EnsureTrayIcon(HWND hwnd) {
    if (g_nidAdded) return;
    AddTrayIcon();
    if (!g_nidAdded && hwnd && g_trayAddTries++ < 5)
        SetTimer(hwnd, 4, 500, nullptr);
}

void RemoveTrayIcon() {
    if (!g_nidAdded) return;
    NOTIFYICONDATAW nid {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_nidAdded = false;
}

// v1.9.0 心跳探测：图标真的还在吗？
// NIM_MODIFY 一个仍然存在的图标返回 TRUE；图标已被系统移除/未添加则返回
// FALSE。这里只重设 callback 消息（值不变），对显示零副作用，代价可忽略。
bool TrayIconAlive() {
    if (!g_hwnd) return false;
    NOTIFYICONDATAW nid {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hwnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAY;
    return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
}

// 提升后进入验证期：由心跳确认 IsPromoted=1 没有造成图标丢失
void BeginTrayAliveCheck() {
    g_trayAliveCheck = true;
    g_aliveOk        = 0;
    g_aliveMiss      = 0;
}

// 把本程序匹配的所有 NotifyIconSettings 子键 IsPromoted 置为指定值，
// 返回是否找到匹配项（value=0 即回滚为系统默认：图标收进「^」折叠区）
static bool WriteIsPromoted(DWORD value) {
    wchar_t exe[MAX_PATH * 2] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    CharUpperBuffW(exe, (DWORD)lstrlenW(exe));
    PCWSTR exeName = wcsrchr(exe, L'\\');
    exeName = exeName ? exeName + 1 : exe;

    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings",
                      0, KEY_ENUMERATE_SUB_KEYS, &root) != ERROR_SUCCESS)
        return false;
    bool found = false;
    wchar_t name[64];
    for (DWORD idx = 0;; ++idx) {
        DWORD len = 64;
        if (RegEnumKeyExW(root, idx, name, &len, nullptr, nullptr, nullptr,
                          nullptr) != ERROR_SUCCESS) break;
        HKEY k = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k)
            != ERROR_SUCCESS) continue;
        wchar_t raw[MAX_PATH * 2] = L"";
        DWORD sz = sizeof(raw), type = 0;
        if (RegQueryValueExW(k, L"ExecutablePath", nullptr, &type, (LPBYTE)raw,
                             &sz) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            // 与 PromoteTrayIcon 相同的路径归一化
            std::wstring p = raw;
            if (p.compare(0, 4, L"\\??\\") == 0) p.erase(0, 4);
            if (!p.empty() && p.front() == L'"') p.erase(0, 1);
            if (!p.empty() && p.back()  == L'"') p.pop_back();
            wchar_t exp[MAX_PATH * 2] = L"";
            if (!ExpandEnvironmentStringsW(p.c_str(), exp, MAX_PATH * 2))
                lstrcpynW(exp, p.c_str(), MAX_PATH * 2);
            CharUpperBuffW(exp, (DWORD)lstrlenW(exp));
            bool same = !lstrcmpW(exp, exe);
            if (!same) {
                size_t n = lstrlenW(exe), m = lstrlenW(exp);
                same = (m > n && lstrcmpW(exp + (m - n), exe) == 0);
            }
            if (!same) {
                PCWSTR en = wcsrchr(exp, L'\\');
                en = en ? en + 1 : exp;
                same = !lstrcmpW(en, exeName);
            }
            if (same) {
                RegSetValueExW(k, L"IsPromoted", 0, REG_DWORD,
                               (const BYTE*)&value, sizeof(value));
                found = true;
            }
        }
        RegCloseKey(k);
    }
    RegCloseKey(root);
    return found;
}

// 提升验证失败后的回滚：IsPromoted 置 0 并重加图标。
// 宁可让图标被收进「^」折叠区，也绝不让图标消失 —— 图标必在是底线
void RollbackTrayPromotion() {
    g_trayPromoted = false;
    if (WriteIsPromoted(0)) {
        RemoveTrayIcon();
        AddTrayIcon();
    }
}

void ShowMainWindow() {
    if (IsIconic(g_hwnd))              ShowWindow(g_hwnd, SW_RESTORE);
    else if (!IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

void HideToTray() {
    ShowWindow(g_hwnd, SW_HIDE);
    if (!g_trayTipShown) {               // 首次最小化提示一次
        g_trayTipShown = true;
        NOTIFYICONDATAW nid {};
        nid.cbSize      = sizeof(nid);
        nid.hWnd        = g_hwnd;
        nid.uID         = 1;
        nid.uFlags      = NIF_INFO;
        nid.uTimeout    = 3000;
        nid.dwInfoFlags = NIIF_INFO;
        lstrcpynW(nid.szInfoTitle, L"Tickly", 64);
        // 图标已提升为常驻时直接告知位置；未能提升（Win10 / 设置项未就绪）
        // 则引导用户去「^」折叠区找，避免找不到入口
        lstrcpynW(nid.szInfo,
                  g_trayPromoted
                      ? L"已关闭到屏幕右下角托盘区，到点提醒仍会弹出；点击托盘小图标可重新打开"
                      : L"已关闭到后台继续运行，到点提醒仍会弹出；若右下角未见图标，请点开任务栏「^」查看",
                  256);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

int PromoteTrayIcon() {
    // 注意：不加 g_nidAdded 守卫 —— 启动时需要“先预写、后加图标”的顺序
    wchar_t exe[MAX_PATH * 2] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    CharUpperBuffW(exe, (DWORD)lstrlenW(exe));
    PCWSTR exeName = wcsrchr(exe, L'\\');
    exeName = exeName ? exeName + 1 : exe;

    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings",
                      0, KEY_ENUMERATE_SUB_KEYS, &root) != ERROR_SUCCESS)
        return TRAY_PROMOTE_NONE;    // Win10 没有此项：无法编程提升，交给气泡文案引导
    int result = TRAY_PROMOTE_NONE;
    wchar_t name[64];
    for (DWORD idx = 0;; ++idx) {
        DWORD len = 64;
        if (RegEnumKeyExW(root, idx, name, &len, nullptr, nullptr, nullptr,
                          nullptr) != ERROR_SUCCESS) break;
        HKEY k = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k)
            != ERROR_SUCCESS) continue;
        wchar_t raw[MAX_PATH * 2] = L"";
        DWORD sz = sizeof(raw), type = 0;
        if (RegQueryValueExW(k, L"ExecutablePath", nullptr, &type, (LPBYTE)raw,
                             &sz) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            // 统一路径形态：展开环境变量、去掉 \\??\\ 前缀与引号、统一大写
            std::wstring p = raw;
            if (p.compare(0, 4, L"\\??\\") == 0) p.erase(0, 4);
            if (!p.empty() && p.front() == L'"') p.erase(0, 1);
            if (!p.empty() && p.back()  == L'"') p.pop_back();
            wchar_t exp[MAX_PATH * 2] = L"";
            if (!ExpandEnvironmentStringsW(p.c_str(), exp, MAX_PATH * 2))
                lstrcpynW(exp, p.c_str(), MAX_PATH * 2);
            CharUpperBuffW(exp, (DWORD)lstrlenW(exp));
            bool same = !lstrcmpW(exp, exe);
            if (!same) {                    // 兼容 \\??\\ 设备路径：整路径后缀匹配
                size_t n = lstrlenW(exe), m = lstrlenW(exp);
                same = (m > n && lstrcmpW(exp + (m - n), exe) == 0);
            }
            if (!same) {                    // 兜底：文件名相同即视为本程序
                PCWSTR en = wcsrchr(exp, L'\\');
                en = en ? en + 1 : exp;
                same = !lstrcmpW(en, exeName);
            }
            if (same) {
                DWORD val = 0, vsz = sizeof(val), vtype = 0;
                if (RegQueryValueExW(k, L"IsPromoted", nullptr, &vtype,
                                     (LPBYTE)&val, &vsz) == ERROR_SUCCESS &&
                    vtype == REG_DWORD && val == 1) {
                    if (result == TRAY_PROMOTE_NONE) result = TRAY_PROMOTE_ALREADY;
                } else {
                    DWORD one = 1;
                    RegSetValueExW(k, L"IsPromoted", 0, REG_DWORD,
                                   (const BYTE*)&one, sizeof(one));
                    result = TRAY_PROMOTE_WROTE;
                }
            }
        }
        RegCloseKey(k);
    }
    RegCloseKey(root);
    return result;
}

// 让「图标常驻右下角」真正生效（修复 v1.8.0 只写注册表不生效的问题）：
//  1) Explorer 在图标添加时读取 IsPromoted，事后写入不会立即反映 —— 所以
//     写入成功后立刻删除并重加图标，强制其按新设置渲染
//  2) Explorer 建立设置子键是异步的，首次运行时子键可能还没出现 —— 找不到
//     时用定时器（id 3）重试，最多 8 次 x 0.6 秒
//  3) v1.9.0：重加失败时立即进入快速重试（此前这条路径失败 = 图标永久
//     消失且无人知晓，是用户「后台运行没有托盘图标」的直接原因之一）；
//     重加成功后进入心跳验证期，验证期内再丢则回滚（见 RollbackTrayPromotion）
void ApplyTrayPromotion(HWND hwnd) {
    if (g_trayRollback) return;          // 已回滚：本运行期走系统默认，不再提升
    for (int round = 0; round < 3; ++round) {
        int r = PromoteTrayIcon();
        if (r == TRAY_PROMOTE_NONE) {        // 子键还没就绪（或 Win10）
            if (hwnd && g_promoteTries++ < 8)
                SetTimer(hwnd, 3, 600, nullptr);
            return;
        }
        g_trayPromoted = true;
        if (r == TRAY_PROMOTE_ALREADY) return;   // 已是常驻状态，无需再动
        if (!hwnd) return;
        RemoveTrayIcon();                        // 重加一次，触发重新读取设置
        AddTrayIcon();
        if (!g_nidAdded) {                       // 重加失败：快速重试兜底
            EnsureTrayIcon(hwnd);
            return;
        }
        BeginTrayAliveCheck();                   // 心跳确认提升无害（10 秒）
    }
    PromoteTrayIcon();                           // 收尾：保证注册表最终为 1
}
