// 自然语言日期时间解析（"明天下午3点" 之类）。
#pragma once

#include "common.h"

struct WhenResult {
    bool        ok      = false;
    long long   due     = 0;
    bool        hasTime = false;
    bool        hasDate = false;   // v0.16：多行逐行识别时区分「仅日期/仅时间」
    std::wstring stripped;   // 剥离日期时间词后的标题
};

WhenResult ParseWhen(const std::wstring& in);

bool ParseWhenMultiline(const std::wstring& text,
                               WhenResult& out, std::wstring& strippedOut);
