// 开机自启（注册表）
#include "common.h"
#include "ui/autostart.h"


// ---------------------------- 开机自启（注册表 HKCU Run） --------------------
static const wchar_t* kRunKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"Tickly";

bool GetAutoStart() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k)
        != ERROR_SUCCESS) return false;
    wchar_t buf[MAX_PATH * 2] = L"";
    DWORD sz = sizeof(buf), type = 0;
    LRESULT r = RegQueryValueExW(k, kRunValue, nullptr, &type, (LPBYTE)buf, &sz);
    // v1.13.7：兼容旧版本（TodoWin）留下的自启项 —— 升级后开机自启不会
    // 静默失效；读到旧值就顺手迁成新值
    bool old = false;
    if (r != ERROR_SUCCESS) {
        sz = sizeof(buf); type = 0;
        r = RegQueryValueExW(k, L"TodoWin", nullptr, &type, (LPBYTE)buf, &sz);
        old = (r == ERROR_SUCCESS && type == REG_SZ);
    }
    RegCloseKey(k);
    if (old) SetAutoStart(true);   // 迁移（内部会清掉旧值）
    return r == ERROR_SUCCESS && type == REG_SZ;
}

void SetAutoStart(bool on) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k)
        != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH * 2] = L"";
        GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)v.c_str(),
                       (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunValue);
    }
    RegDeleteValueW(k, L"TodoWin");   // v1.13.7：清掉改名前的旧自启项
    RegCloseKey(k);
}
