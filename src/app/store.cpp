// 持久化（tasks.txt：任务 + 设置项）
//
// ─────────────────────────────────────────────────────────────────────────────
//  数据格式 v4（UTF-8，带 BOM；每行一条记录，字段用 TAB 分隔）
//
//    V<TAB>4                                       格式版本（v4 起；旧文件没有本行）
//    E<TAB>1                                       兼容旧版：文本已转义（新版本恒为 1）
//    S<TAB>b<TAB>b<TAB>b<TAB>b<TAB>b              完成分组展开：本周/上周/本月/上月/其他
//      <TAB>b<TAB>b<TAB>b<TAB>b<TAB>b              未完成分组展开：已逾期/今天/明天/更晚/无日期
//      <TAB>b                                       已完成区收纳
//    C<TAB>b<TAB>min<TAB>b<TAB>mod<TAB>vk<TAB>mod<TAB>vk
//                                                  提醒开关/提前分钟/关闭到托盘/两个全局快捷键
//    W<TAB>max<TAB>left<TAB>top<TAB>right<TAB>bottom 窗口位置（max=1 表示最大化）
//    F<TAB>size<TAB>r<TAB>g<TAB>b ...              字体与主题（见 LoadTasks 的字段索引）
//    T<TAB>done<TAB>doneAt<TAB>due<TAB>hasTime<TAB>notified<TAB>text
//
//  规则：
//  · 写入端恒定输出全部已知字段；读取端按“字段够就用、不够用默认值”处理，
//    因此旧版本能读新文件（忽略未知行/多余字段），新版本也能读旧文件。
//  · text 内的转义：\\ → 反斜杠，\n → 换行，\t → 空格，\r 丢弃。
//  · 未知记录（首字段不认识）忽略，保持向前兼容。
//  · 写入是原子的：先写 tasks.txt.tmp，旧文件备份为 tasks.txt.bak，
//    再用 MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH) 替换，断电/崩溃不会留下半截文件。
// ─────────────────────────────────────────────────────────────────────────────
#include "common.h"
#include "app/store.h"
#include "app/rows.h"
#include "ui/hotkeys.h"
#include "ui/reminder.h"
#include "ui/context.h"
#include "core/task.h"
#include "core/text_util.h"
#include "ui/theme.h"

#include <array>
#include <limits>
#include <optional>
#include <string_view>

#include <shlobj.h>      // SHGetKnownFolderPath（%APPDATA% 的现代取法）

std::vector<Task> g_tasks;

namespace {

constexpr int kDataVersion = 4;   // 本程序写出的格式版本

// ---------------------------- 路径 ------------------------------------------
std::wstring ExeDir() {
    std::wstring path(MAX_PATH * 4, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
    if (n == 0 || n >= path.size()) return L".";
    path.resize(n);
    const size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? L"." : path.substr(0, p);
}

// %APPDATA%（RoamingAppData）：SHGetKnownFolderPath 不依赖环境变量，重定向场景更可靠
std::wstring AppDataDir() {
    std::wstring out;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p)
        out.assign(p);
    if (p) CoTaskMemFree(p);
    return out;
}

bool FileExists(std::wstring_view path) {
    return GetFileAttributesW(std::wstring(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 存在但为空：视为“没有数据”（首次运行会被 OPEN_ALWAYS 创建出 0 字节文件）
bool IsEmptyFile(std::wstring_view path) {
    WIN32_FILE_ATTRIBUTE_DATA d {};
    if (!GetFileAttributesExW(std::wstring(path).c_str(), GetFileExInfoStandard, &d))
        return false;
    return d.nFileSizeHigh == 0 && d.nFileSizeLow == 0;
}

bool CanWrite(std::wstring_view path) {
    HANDLE h = CreateFileW(std::wstring(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// 数据文件位置：优先 exe 同目录（便携），不可写时退回 %APPDATA%\Tickly。
// 首次运行时按“历史位置”顺序找一份现成数据并复制过来（复制而非移动，
// 源文件保留，便于回退）。查找顺序：当前目录 → %APPDATA%\Tickly → %APPDATA%\TodoWin
std::wstring ResolveDataPath() {
    static const std::wstring cached = [] {
        const std::wstring exePath = ExeDir() + L"\\tasks.txt";
        if (FileExists(exePath) && !IsEmptyFile(exePath)) return exePath;

        const std::wstring appdata = AppDataDir();
        const std::array<std::wstring, 3> seeds = {
            std::wstring(L".") + L"\\tasks.txt",     // 从命令行 / 快捷方式启动时的当前目录
            appdata + L"\\Tickly\\tasks.txt",        // v1.13.7+ 的数据目录
            appdata + L"\\TodoWin\\tasks.txt",       // 更名前的旧目录
        };
        for (const std::wstring& src : seeds) {
            if (!FileExists(src) || IsEmptyFile(src)) continue;
            if (CopyFileW(src.c_str(), exePath.c_str(), FALSE)) return exePath;
        }

        if (CanWrite(exePath)) return exePath;    // 全新安装：exe 同目录
        const std::wstring dir = appdata + L"\\Tickly";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\tasks.txt";
    }();
    return cached;
}

// ---------------------------- 读写 ------------------------------------------
std::optional<std::string> ReadFileBytes(std::wstring_view path) {
    HANDLE h = CreateFileW(std::wstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size {};
    std::string data;
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20)) {
        data.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ReadFile(h, data.data(), (DWORD)data.size(), &got, nullptr);
        data.resize(got);
    }
    CloseHandle(h);
    return data;
}

// 原子写入：tmp → (旧文件备份 .bak) → MoveFileEx 替换。
// 直接覆盖写在崩溃/断电时会留下半截文件，导致全部任务丢失
bool WriteFileBytesAtomic(std::wstring_view path, std::string_view bytes) {
    const std::wstring target(path);
    const std::wstring tmp = target + L".tmp";
    const std::wstring bak = target + L".bak";

    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t off = 0;
    while (off < bytes.size()) {
        DWORD wrote = 0;
        const DWORD chunk = (DWORD)std::min<size_t>(bytes.size() - off, 1u << 20);
        if (!WriteFile(h, bytes.data() + off, chunk, &wrote, nullptr) || wrote == 0) {
            ok = false;
            break;
        }
        off += wrote;
    }
    if (ok) ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }

    if (FileExists(target)) CopyFileW(target.c_str(), bak.c_str(), FALSE);
    if (MoveFileExW(tmp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    // 个别文件系统不支持 WRITE_THROUGH：退一步再试，最后退回直接覆盖
    if (MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
        return true;
    DeleteFileW(tmp.c_str());
    h = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
    CloseHandle(h);
    return wrote == bytes.size();
}

// ---------------------------- 字段工具 --------------------------------------
// 按 TAB 切分（不分配：返回指向 line 的视图）
std::vector<std::wstring_view> SplitViews(const std::wstring& line, size_t from) {
    std::vector<std::wstring_view> out;
    out.reserve(8);
    size_t p = from;
    for (;;) {
        const size_t q = line.find(L'\t', p);
        if (q == std::wstring::npos) {
            out.emplace_back(line.data() + p, line.size() - p);
            break;
        }
        out.emplace_back(line.data() + p, q - p);
        p = q + 1;
    }
    return out;
}

// 十进制整数（不分配、无 locale 依赖）；非法返回 def
long long ToInt(std::wstring_view s, long long def = 0) {
    if (s.empty()) return def;
    size_t i = 0;
    bool neg = false;
    if (s[0] == L'+' || s[0] == L'-') { neg = (s[0] == L'-'); i = 1; }
    if (i >= s.size()) return def;
    long long v = 0;
    for (; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c < L'0' || c > L'9') return def;
        if (v > ((std::numeric_limits<long long>::max)() - 9) / 10) return def;   // 溢出：按非法处理
        v = v * 10 + (c - L'0');
    }
    return neg ? -v : v;
}

bool ToBool(std::wstring_view s, bool def = true) {
    if (s.empty()) return def;
    return s != L"0";
}

// 保存时的转义：\\ → \\\\，换行 → \\n，TAB → 空格（分隔符），\r 丢弃
std::wstring EscapeForLine(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (const wchar_t c : s) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n";  break;
        case L'\t': out += L' ';    break;
        case L'\r': break;
        default:    out += c;       break;
        }
    }
    return out;
}

}  // namespace

// ---------------------------- 保存 ------------------------------------------
void SaveTasks() {
    std::string out;
    out.reserve(256 + g_tasks.size() * 64);
    out += "\xEF\xBB\xBF";              // UTF-8 BOM（记事本直接打开不乱码）
    out += "V\t" + std::to_string(kDataVersion) + "\r\n";
    // E\t1：v1.12.0 起的“文本已转义”标记。新版本恒定输出，旧版本据此还原 \n，
    // 保证「新版写出的文件用旧版打开」也不会显示成字面 \n
    out += "E\t1\r\n";

    // S：分组展开 10 个布尔（完成 5 + 未完成 5）+ 已完成区收纳
    out += "S\t";
    for (int g = 0; g < 10; ++g) {
        out += (g_groupOpen[g] ? "1" : "0");
        out += "\t";
    }
    out += (g_doneCollapsed ? "1" : "0");
    out += "\r\n";

    // C：提醒/行为设置 + 两个全局快捷键（mod 固定 Alt，仍写入以兼容旧版）
    out += "C\t";
    out += (g_remindOn ? "1" : "0");
    out += "\t" + std::to_string(g_remindMin);
    out += "\t";
    out += (g_closeToTray ? "1" : "0");
    out += "\t" + std::to_string(g_hkShowMod);
    out += "\t" + std::to_string(g_hkShowVk);
    out += "\t" + std::to_string(g_hkCapMod);
    out += "\t" + std::to_string(g_hkCapVk);
    out += "\r\n";

    // W：窗口位置（ui 层在写盘前已同步到 g_wp*；无窗口/未记录则略过）
    if (g_wpSaved) {
        out += "W\t";
        out += (g_wpMax ? "1" : "0");
        out += "\t" + std::to_string((long long)g_wpRect.left);
        out += "\t" + std::to_string((long long)g_wpRect.top);
        out += "\t" + std::to_string((long long)g_wpRect.right);
        out += "\t" + std::to_string((long long)g_wpRect.bottom);
        out += "\r\n";
    }

    // F：任务/分组/日期/已完成分组 的字号+颜色，主题模式
    // 顺序与旧版一致（旧版读到多余字段会忽略），新增字段一律追加在末尾
    out += "F\t";
    out += std::to_string(g_taskFontSize);
    out += "\t" + std::to_string((int)GetRValue(g_taskFontCol));
    out += "\t" + std::to_string((int)GetGValue(g_taskFontCol));
    out += "\t" + std::to_string((int)GetBValue(g_taskFontCol));
    out += "\t" + std::to_string(g_grpFontSize);
    out += "\t" + std::to_string((int)GetRValue(g_grpFontCol));
    out += "\t" + std::to_string((int)GetGValue(g_grpFontCol));
    out += "\t" + std::to_string((int)GetBValue(g_grpFontCol));
    out += "\t" + std::to_string(g_dtFontSize);
    out += "\t" + std::to_string((int)GetRValue(g_dtFontCol));
    out += "\t" + std::to_string((int)GetGValue(g_dtFontCol));
    out += "\t" + std::to_string((int)GetBValue(g_dtFontCol));
    out += "\t" + std::to_string(g_themeMode);
    out += "\t" + std::to_string(g_doneGrpFontSize);
    out += "\t" + std::to_string((int)GetRValue(g_doneGrpFontCol));
    out += "\t" + std::to_string((int)GetGValue(g_doneGrpFontCol));
    out += "\t" + std::to_string((int)GetBValue(g_doneGrpFontCol));
    out += "\r\n";

    // T：done / doneAt / due / hasTime / notified / text
    for (const Task& t : g_tasks) {
        out += "T\t";
        out += (t.done ? "1" : "0");
        out += "\t" + std::to_string(t.doneAt);
        out += "\t" + std::to_string(t.due);
        out += "\t";
        out += (t.hasTime ? "1" : "0");
        out += "\t";
        out += (t.notified ? "1" : "0");
        out += "\t";
        out += WideToUtf8(EscapeForLine(t.text));
        out += "\r\n";
    }

    WriteFileBytesAtomic(ResolveDataPath(), out);
}

// ---------------------------- 读取 ------------------------------------------
void LoadTasks() {
    // 先全部恢复默认值：文件缺字段/整行缺失时用这里的值
    g_tasks.clear();
    for (int g = 0; g < 10; ++g) g_groupOpen[g] = true;
    g_doneCollapsed = true;
    g_remindOn      = true;
    g_remindMin     = 15;
    g_closeToTray   = true;
    g_hkShowMod     = MOD_ALT;
    g_hkShowVk      = 'Q';
    g_hkCapMod      = MOD_ALT;
    g_hkCapVk       = '1';
    g_themeMode     = 2;
    g_wpSaved       = false;
    g_wpMax         = false;

    const std::optional<std::string> file = ReadFileBytes(ResolveDataPath());
    if (!file || file->empty()) return;

    const std::string& data = *file;
    size_t pos = 0;
    if (data.starts_with("\xEF\xBB\xBF")) pos = 3;   // C++20

    int  fileVer = 0;          // V 行声明的格式版本
    bool escapeText = false;   // 文本是否按 \\n / \\\\ 转义
    bool recognized = false;   // 是否至少认出一行合法记录（损坏检测用）
    const size_t est = data.size() / 48 + 1;
    g_tasks.reserve(est < 4096 ? est : 4096);

    while (pos < data.size()) {
        const size_t eol = data.find('\n', pos);
        std::string line = data.substr(pos,
            (eol == std::string::npos ? data.size() : eol) - pos);
        pos = (eol == std::string::npos) ? data.size() : eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 2) continue;

        std::wstring w = Utf8ToWide(line);
        if (w.size() < 2) continue;

        switch (w[0]) {
        case L'V': {                                   // 版本行（v4+）
            if (w[1] != L'\t') break;
            recognized = true;
            fileVer = (int)ToInt(std::wstring_view(w).substr(2), 0);
            break;
        }
        case L'E': {                                   // 转义标记（v1.12.0+）
            if (w[1] != L'\t') break;
            recognized = true;
            escapeText = true;
            break;
        }
        case L'S': {
            if (w[1] != L'\t') break;
            recognized = true;
            const std::vector<std::wstring_view> f = SplitViews(w, 2);
            auto on = [&](size_t i) { return i >= f.size() || ToBool(f[i]); };
            for (int g = 0; g < 10; ++g) g_groupOpen[g] = on((size_t)g);
            g_doneCollapsed = on(10);
            break;
        }
        case L'W': {
            if (w[1] != L'\t') break;
            recognized = true;
            const std::vector<std::wstring_view> f = SplitViews(w, 2);
            if (f.size() < 5) break;
            RECT r {};
            r.left   = (LONG)ToInt(f[1]);
            r.top    = (LONG)ToInt(f[2]);
            r.right  = (LONG)ToInt(f[3]);
            r.bottom = (LONG)ToInt(f[4]);
            if (r.right > r.left && r.bottom > r.top) {
                g_wpRect  = r;
                g_wpMax   = ToBool(f[0], false);
                g_wpSaved = true;
            }
            break;
        }
        case L'F': {
            if (w[1] != L'\t') break;
            recognized = true;
            const std::vector<std::wstring_view> f = SplitViews(w, 2);
            auto col = [&](size_t ir, size_t ig, size_t ib, COLORREF def) -> COLORREF {
                if (f.size() <= ib) return def;
                const long long cr = ToInt(f[ir], -1);
                const long long cg = ToInt(f[ig], -1);
                const long long cb = ToInt(f[ib], -1);
                if (cr < 0 || cr > 255 || cg < 0 || cg > 255 || cb < 0 || cb > 255)
                    return def;
                return RGB((int)cr, (int)cg, (int)cb);
            };
            auto sizeIn = [&](size_t i, int lo, int hi, int def) {
                if (f.size() <= i) return def;
                const long long v = ToInt(f[i], def);
                return (v >= lo && v <= hi) ? (int)v : def;
            };
            if (!f.empty()) g_taskFontSize  = sizeIn(0, 12, 22, g_taskFontSize);
            g_taskFontCol  = col(1, 2, 3, g_taskFontCol);
            g_grpFontSize  = sizeIn(4, 10, 18, g_grpFontSize);
            g_grpFontCol   = col(5, 6, 7, g_grpFontCol);
            g_dtFontSize   = sizeIn(8, 10, 18, g_dtFontSize);
            g_dtFontCol    = col(9, 10, 11, g_dtFontCol);
            if (f.size() > 12) {
                const long long tm = ToInt(f[12], g_themeMode);
                if (tm >= 0 && tm <= 2) g_themeMode = (int)tm;
            }
            g_doneGrpFontSize = sizeIn(13, 10, 18, g_doneGrpFontSize);
            g_doneGrpFontCol  = col(14, 15, 16, g_doneGrpFontCol);
            break;
        }
        case L'C': {
            if (w[1] != L'\t') break;
            recognized = true;
            const std::vector<std::wstring_view> f = SplitViews(w, 2);
            if (f.size() >= 1) g_remindOn = ToBool(f[0]);
            if (f.size() >= 2) {
                const long long v = ToInt(f[1], g_remindMin);
                g_remindMin = (int)(v < 0 ? 0 : (v > 10080 ? 10080 : v));
            }
            if (f.size() >= 3) g_closeToTray = ToBool(f[2]);
            if (f.size() >= 7) {   // 快捷键：只保留主键，修饰键固定 Alt
                const long long v1 = ToInt(f[4], 0);
                const long long v2 = ToInt(f[6], 0);
                if (v1 >= 1 && v1 <= 254) { g_hkShowMod = MOD_ALT; g_hkShowVk = (UINT)v1; }
                if (v2 >= 1 && v2 <= 254) { g_hkCapMod  = MOD_ALT; g_hkCapVk  = (UINT)v2; }
            }
            break;
        }
        case L'T': {
            if (w[1] != L'\t') break;
            recognized = true;
            const std::vector<std::wstring_view> f = SplitViews(w, 2);
            Task t;
            if (f.size() >= 6) {
                t.done     = ToBool(f[0], false);
                t.doneAt   = ToInt(f[1]);
                t.due      = ToInt(f[2]);
                t.hasTime  = ToBool(f[3], false);
                t.notified = ToBool(f[4], false);
                // 兼容：旧格式可能把 TAB 留在文本里（历史手工编辑），把多余字段接回去
                std::wstring text(f[5]);
                for (size_t i = 6; i < f.size(); ++i) { text += L'\t'; text += f[i]; }
                t.text = NormalizeMultiline(escapeText ? UnescapeText(text) : text);
            } else if (f.size() == 5) {
                // v2 布局：done / doneAt / due / hasTime / text
                t.done    = ToBool(f[0], false);
                t.doneAt  = ToInt(f[1]);
                t.due     = ToInt(f[2]);
                t.hasTime = ToBool(f[3], false);
                t.text    = NormalizeMultiline(std::wstring(f[4]));
            } else {
                break;
            }
            if (!t.text.empty()) g_tasks.push_back(std::move(t));
            break;
        }
        default: {
            // v1 兼容：`0<TAB>text` / `1<TAB>text`
            if (w.size() > 1 && w[1] == L'\t' && (w[0] == L'0' || w[0] == L'1')) {
                recognized = true;
                Task t;
                t.done = (w[0] == L'1');
                t.text = NormalizeMultiline(std::wstring(w).substr(2));
                if (!t.text.empty()) g_tasks.push_back(std::move(t));
            }
            break;
        }
        }
    }

    // 未来版本的文件：字段仍然按已知规则解析，写回时保留一份 .bak，
    // 因此降级不会直接毁掉数据（由 WriteFileBytesAtomic 完成备份）
    (void)fileVer;

    // 损坏保护：文件有内容却一行都认不出来（编码被破坏、被别的程序写坏、
    // 或者根本不是本程序的文件）。若就这么以空列表启动，下一次保存会把
    // 原文件覆盖成只剩设置行 —— 数据就真没了。这里改名留证并提示一次。
    if (!recognized && data.find_first_not_of(" \t\r\n") != std::string::npos) {
        const std::wstring path = ResolveDataPath();
        const std::wstring bad  = path + L".bad";
        MoveFileExW(path.c_str(), bad.c_str(), MOVEFILE_REPLACE_EXISTING);
        const std::wstring msg =
            L"数据文件无法解析，已改名保留：\n" + bad +
            L"\n\nTickly 将以空列表启动；修复该文件后可以改回 tasks.txt 再用。";
        MessageBoxW(g_hwnd, msg.c_str(), L"Tickly 数据文件损坏",
                    MB_ICONWARNING | MB_OK);
    }
}
