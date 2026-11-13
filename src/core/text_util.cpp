// 文本工具
#include "common.h"
#include "core/text_util.h"


std::wstring Trim(const std::wstring& s) {
    static const wchar_t* kWs = L" \t\r\n\xA0\x3000";
    size_t b = s.find_first_not_of(kWs);
    if (b == std::wstring::npos) return std::wstring();
    size_t e = s.find_last_not_of(kWs);
    return s.substr(b, e - b + 1);
}

std::wstring NormalizeMultiline(const std::wstring& in) {
    std::wstring out;
    size_t p = 0;
    for (;;) {
        size_t e = in.find(L'\n', p);
        std::wstring line = in.substr(p,
            (e == std::wstring::npos) ? std::wstring::npos : e - p);
        line.erase(std::remove(line.begin(), line.end(), L'\r'), line.end());
        line = Trim(line);
        line = CollapseWs(line);
        if (!line.empty()) {
            if (!out.empty()) out += L'\n';
            out += line;
        }
        if (e == std::wstring::npos) break;
        p = e + 1;
    }
    if (out.size() > 4000) out.resize(4000);
    return out;
}

// v0.16：内部 \n 换行 -> EDIT 控件要求的 \r\n（打开行内编辑框载入文本用）。
// EDIT 控件只认 \r\n 为换行，纯 \n 会全部挤在一行
std::wstring ToCrLf(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size() + 16);
    for (wchar_t c : s) {
        if (c == L'\r') continue;      // 内部文本不应有 \r，防御性跳过
        if (c == L'\n') o += L"\r\n";
        else            o += c;
    }
    return o;
}

// tasks.txt 中任务文本的换行转义还原（仅 E\t1 标记的文件使用）：
// \\n -> 换行、\\\\ -> 反斜杠；v1.12.0 之前的历史文件不做此还原
std::wstring UnescapeText(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[i + 1];
            if (n == L'n') { o += L'\n'; ++i; continue; }
            if (n == L'\\') { o += L'\\'; ++i; continue; }
        }
        o += s[i];
    }
    return o;
}

// 将连续空白折叠为单个空格（用于剥离日期词后的标题整理）
std::wstring CollapseWs(const std::wstring& s) {
    std::wstring o;
    bool sp = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xA0 || c == 0x3000) {
            sp = !o.empty();
            continue;
        }
        if (sp) { o += L' '; sp = false; }
        o += c;
    }
    return o;
}

bool IsDigitW(wchar_t c) { return c >= L'0' && c <= L'9'; }

// 从 s[i] 起读取最多 maxDigits 位十进制数；无数字返回 -1
int DigitAt(const std::wstring& s, size_t& i, int maxDigits) {
    int v = 0, n = 0;
    while (i < s.size() && n < maxDigits && IsDigitW(s[i])) {
        v = v * 10 + (s[i] - L'0');
        ++i; ++n;
    }
    return n ? v : -1;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n,
                            nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0],
                            n, nullptr, nullptr);
    return s;
}
