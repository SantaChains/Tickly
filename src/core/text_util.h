// 文本工具：宽窄字符串转换、空白整理、多行转义。
#pragma once

#include "common.h"

std::wstring Trim(const std::wstring& s);

std::wstring NormalizeMultiline(const std::wstring& in);

std::wstring ToCrLf(const std::wstring& s);

std::wstring UnescapeText(const std::wstring& s);

std::wstring CollapseWs(const std::wstring& s);   // 前置声明（定义在后）

bool IsDigitW(wchar_t c);

int DigitAt(const std::wstring& s, size_t& i, int maxDigits);

std::string WideToUtf8(const std::wstring& w);

std::wstring Utf8ToWide(const std::string& s);

std::string ToUtf8(const std::wstring& w);
