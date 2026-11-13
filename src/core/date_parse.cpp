// 自然语言日期时间解析
#include "common.h"
#include "core/date_parse.h"
#include "core/text_util.h"
#include "core/time_util.h"


static const struct { const wchar_t* tok; int delta; } kDayKw[] = {
    {L"大后天", 3}, {L"后天", 2}, {L"明天", 1}, {L"明日", 1},
    {L"今天", 0}, {L"今日", 0},
};

enum TPrefix { P_NONE = 0, P_EARLY, P_MORNING, P_NOON, P_PM, P_TONIGHT, P_TOM_NIGHT };
static const struct { const wchar_t* tok; int type; } kTimePf[] = {
    {L"凌晨", P_EARLY}, {L"清晨", P_MORNING}, {L"早上", P_MORNING},
    {L"早晨", P_MORNING}, {L"上午", P_MORNING}, {L"中午", P_NOON},
    {L"午后", P_PM}, {L"下午", P_PM}, {L"傍晚", P_PM}, {L"晚上", P_PM},
    {L"夜里", P_PM}, {L"今晚", P_TONIGHT}, {L"明晚", P_TOM_NIGHT},
};

WhenResult ParseWhen(const std::wstring& in) {
    WhenResult r;
    if (in.size() < 2) return r;
    tm now = LocalTmOf((long long)time(nullptr));
    int  nowY = now.tm_year + 1900;
    long long nowEp = (long long)time(nullptr);
    long long todayMid = TodayMidnight();

    // ---------- 1. 日期 ----------
    // v0.20：两轮扫描 —— 第一轮只找「明确日期」（具体年月日），第二轮才
    // 匹配相对词（今天/下周三等）。文本同时出现两者时，如
    // 「下周三(10月21)上午9:00开会」—— 括号里写明的具体日期才是本意
    // （口头相对词可能说错），以明确日期为准，修复 21 号被「下周三」
    // 推算结果覆盖的问题
    bool   hasDate = false;
    size_t dA = 0, dB = 0;
    int    dY = 0, dM = 0, dD = 0;
    long long dayDelta = -1;

    for (int pass = 0; pass < 2 && !hasDate; ++pass) {
      for (size_t i = 0; i < in.size(); ++i) {
        // (a) 今天/明天/后天/大后天（相对词，仅第二轮）
        if (pass == 1) {
        bool hit = false;
        for (const auto& kw : kDayKw) {
            size_t L2 = wcslen(kw.tok);
            if (in.compare(i, L2, kw.tok) == 0) {
                dayDelta = kw.delta; dA = i; dB = i + L2;
                hasDate = true; hit = true; break;
            }
        }
        if (hit) break;
        }

        // (b) (本|这|下)?(周|星期|礼拜)X / 周末（相对词，仅第二轮）
        if (pass == 1) {
        {
            size_t j = i;
            int pref = 0; // 0 无, 1 本/这, 2 下
            if (in[j] == L'本' || in[j] == L'这') { pref = 1; ++j; }
            else if (in[j] == L'下') { pref = 2; ++j; }
            int wdLen = 0;
            if (in.compare(j, 1, L"周") == 0) wdLen = 1;
            else if (j + 1 < in.size() &&
                     (in.compare(j, 2, L"星期") == 0 || in.compare(j, 2, L"礼拜") == 0))
                wdLen = 2;
            if (wdLen) {
                size_t k = j + (size_t)wdLen;
                int target = -1;
                wchar_t c = (k < in.size()) ? in[k] : 0;
                switch (c) {
                    case L'一': target = 1; break;
                    case L'二': target = 2; break;
                    case L'三': target = 3; break;
                    case L'四': target = 4; break;
                    case L'五': target = 5; break;
                    case L'六': target = 6; break;
                    case L'日': case L'天': target = 0; break;
                    case L'末': target = 6; break;   // 周末 -> 周六
                }
                if (target >= 0) {
                    ++k;
                    long long dlt = (target - now.tm_wday + 7) % 7;
                    if (pref == 2) dlt += 7;
                    dayDelta = dlt; dA = i; dB = k;
                    hasDate = true; break;
                }
            }
        }
        }

        // (b2) 「下月/下个月 + D号(D日)」（相对词，第二轮）
        //      「今年/明年 + M月D日」（明确日期，第一轮）（v1.10.1）
        {
            int mOff = 0, yrAbs = -1;
            size_t j = i;
            if (in.compare(j, 3, L"下个月") == 0)    { mOff = 1; j += 3; }
            else if (in.compare(j, 2, L"下月") == 0) { mOff = 1; j += 2; }
            else if (in.compare(j, 2, L"明年") == 0) { yrAbs = nowY + 1; j += 2; }
            else if (in.compare(j, 2, L"今年") == 0) { yrAbs = nowY; j += 2; }
            if ((mOff && pass == 1) || (yrAbs >= 0 && pass == 0)) {
                while (j < in.size() && in[j] == L' ') ++j;
                size_t k = j;
                if (mOff) {   // 下月 + 裸「D号/D日」-> 下个月该日（该月无此日则顺延）
                    int da = DigitAt(in, k, 2);
                    if (da > 0 && k < in.size() &&
                        (in[k] == L'日' || in[k] == L'号')) {
                        ++k;
                        int Y = nowY, Mo = now.tm_mon + 1 + mOff;
                        if (Mo > 12) { Mo = 1; ++Y; }
                        for (int n = 0; n < 12; ++n) {
                            if (da <= DaysInMonth(Y, Mo)) break;
                            if (++Mo > 12) { Mo = 1; ++Y; }
                        }
                        if (da <= DaysInMonth(Y, Mo)) {
                            dY = Y; dM = Mo; dD = da;
                            dA = i; dB = k; hasDate = true; break;
                        }
                    }
                } else {      // 今年/明年 + M月D日 -> 按字面年份
                    int mo = DigitAt(in, k, 2);
                    if (mo >= 1 && mo <= 12 && k < in.size() && in[k] == L'月') {
                        ++k;
                        int da = DigitAt(in, k, 2);
                        if (da > 0 && k < in.size() &&
                            (in[k] == L'日' || in[k] == L'号')) {
                            ++k;
                            if (da <= DaysInMonth(yrAbs, mo)) {
                                dY = yrAbs; dM = mo; dD = da;
                                dA = i; dB = k; hasDate = true; break;
                            }
                        }
                    }
                }
            }
        }

        // (c) 数字日期：YYYY-M-D / YYYY年M月D日（明确日期，仅第一轮）
        if (pass == 0 && IsDigitW(in[i])) {
            size_t j = i;
            int y = DigitAt(in, j, 4);
            if (y >= 1000 && j < in.size() &&
                (in[j] == L'年' || in[j] == L'-' || in[j] == L'/' || in[j] == L'.')) {
                wchar_t sep = in[j];
                size_t k = j + 1;
                int mo = DigitAt(in, k, 2);
                int da = -1;
                if (mo > 0 && k < in.size() &&
                    (sep == L'年' ? in[k] == L'月' : in[k] == sep)) {
                    ++k;
                    da = DigitAt(in, k, 2);
                    if (da > 0 && k < in.size() && (in[k] == L'日' || in[k] == L'号')) ++k;
                    if (mo >= 1 && mo <= 12 && da >= 1 && da <= DaysInMonth(y, mo) &&
                        y >= 1970 && y <= 2999) {
                        dY = y; dM = mo; dD = da;
                        dA = i; dB = k; hasDate = true; break;
                    }
                }
            }
            // M月D日
            {
                size_t j = i;
                int mo = DigitAt(in, j, 2);
                if (mo > 0 && j < in.size() && in[j] == L'月') {
                    size_t k = j + 1;
                    int da = DigitAt(in, k, 2);
                    if (da > 0 && k < in.size() && (in[k] == L'日' || in[k] == L'号')) ++k;
                    if (mo >= 1 && mo <= 12 && da >= 1 && da <= DaysInMonth(nowY, mo)) {
                        dY = 0; dM = mo; dD = da;
                        dA = i; dB = k; hasDate = true; break;
                    }
                }
            }
            // M-D / M/D（不含小数点，避免误伤 3.5 之类）
            {
                size_t j = i;
                int mo = DigitAt(in, j, 2);
                if (mo > 0 && j < in.size() && (in[j] == L'-' || in[j] == L'/')) {
                    size_t k = j + 1;
                    int da = DigitAt(in, k, 2);
                    if (mo >= 1 && mo <= 12 && da >= 1 && da <= DaysInMonth(nowY, mo)) {
                        dY = 0; dM = mo; dD = da;
                        dA = i; dB = k; hasDate = true; break;
                    }
                }
            }
            // (d) 裸「D日/D号」（明确日期，仅第一轮）（v1.10.1）：
            //     本月该日；该日已过或本月无此日 -> 逐月顺延到未来最近一次。
            //     修复「15号9点」日期段匹配不到、时间「9点」被单独识别
            //     而落到今天的问题。不匹配更长数字串的中段：「115号」
            //     不会被拆出「5号」
            if (pass == 0 && (i == 0 || !IsDigitW(in[i - 1]))) {
                size_t j = i;
                int da = DigitAt(in, j, 2);
                if (da > 0 && da <= 31 && j < in.size() &&
                    (in[j] == L'日' || in[j] == L'号')) {
                    size_t k = j + 1;
                    int Y = nowY, Mo = now.tm_mon + 1;
                    for (int n = 0; n < 12; ++n) {
                        if (da <= DaysInMonth(Y, Mo) &&
                            MakeEpoch(Y, Mo, da, 0, 0) >= todayMid) {
                            dY = Y; dM = Mo; dD = da;
                            dA = i; dB = k; hasDate = true; break;
                        }
                        if (++Mo > 12) { Mo = 1; ++Y; }
                    }
                    if (hasDate) break;
                }
            }
        }
      }
    }

    // v0.20：明确日期与相对词同现时，把紧邻的相对日期词与括号一并纳入
    // 消费区间 —— 「下周三(10月21)」日期取括号里的 10月21，剥离后标题
    // 不残留「下周三(」「）」碎片；「10月21日(下周三)」「10月21 下周三」
    // 同样处理（日期一律以明确日期为准）
    if (hasDate && dayDelta < 0) {
        auto isWkDay = [](wchar_t c) {
            switch (c) {
                case L'一': case L'二': case L'三': case L'四': case L'五':
                case L'六': case L'日': case L'天': case L'末': return true;
            }
            return false;
        };
        // 反向匹配相对日期词（词尾紧贴 end），返回词起点
        auto revRel = [&](size_t end) -> size_t {
            if (end == 0) return std::wstring::npos;
            for (const auto& kw : kDayKw) {
                size_t L2 = wcslen(kw.tok);
                if (end >= L2 && in.compare(end - L2, L2, kw.tok) == 0)
                    return end - L2;
            }
            // (上|本|这|下)?(周|星期|礼拜)X
            if (isWkDay(in[end - 1])) {
                size_t q = end - 1, ws = std::wstring::npos;
                if (q >= 1 && in[q - 1] == L'周') ws = q - 1;
                else if (q >= 2 &&
                         ((in[q - 2] == L'星' && in[q - 1] == L'期') ||
                          (in[q - 2] == L'礼' && in[q - 1] == L'拜')))
                    ws = q - 2;
                if (ws != std::wstring::npos) {
                    if (ws >= 1) {
                        wchar_t pf = in[ws - 1];
                        if (pf == L'上' || pf == L'本' ||
                            pf == L'这' || pf == L'下') --ws;
                    }
                    return ws;
                }
            }
            return std::wstring::npos;
        };
        // 正向匹配相对日期词（从 start 起），返回词尾（不含）
        auto fwdRel = [&](size_t start) -> size_t {
            if (start >= in.size()) return std::wstring::npos;
            for (const auto& kw : kDayKw) {
                size_t L2 = wcslen(kw.tok);
                if (in.compare(start, L2, kw.tok) == 0) return start + L2;
            }
            size_t j = start;
            if (j < in.size() && (in[j] == L'上' || in[j] == L'本' ||
                                  in[j] == L'这' || in[j] == L'下')) ++j;
            int wdLen = 0;
            if (j < in.size() && in[j] == L'周') wdLen = 1;
            else if (j + 1 < in.size() &&
                     (in.compare(j, 2, L"星期") == 0 ||
                      in.compare(j, 2, L"礼拜") == 0)) wdLen = 2;
            if (wdLen && j + wdLen < in.size() && isWkDay(in[j + wdLen]))
                return j + (size_t)wdLen + 1;
            return std::wstring::npos;
        };
        // 前向：日期左侧的空白/左括号后若紧贴相对词，一并消费
        //（「下周三(10月21)」「下周三 (10月21)」）
        {
            size_t p = dA;
            while (p > 0 && (in[p - 1] == L' ' || in[p - 1] == L'(' ||
                             in[p - 1] == L'（')) --p;
            if (p < dA) {
                size_t ws = revRel(p);
                if (ws != std::wstring::npos) dA = ws;
            }
        }
        // 后向：消费区间内含左括号时右括号一并消费（括号成对剥除）；
        // 否则日期右侧的空白/左括号后紧贴相对词时消费到词尾
        //（「10月21日(下周三)」「10月21 下周三」）
        {
            bool hasParen = false;
            for (size_t p2 = dA; p2 < dB && !hasParen; ++p2)
                if (in[p2] == L'(' || in[p2] == L'（') hasParen = true;
            size_t q = dB;
            if (hasParen && q < in.size() &&
                (in[q] == L')' || in[q] == L'）')) {
                ++q;
            } else {
                while (q < in.size() && (in[q] == L' ' || in[q] == L'(' ||
                                         in[q] == L'（')) ++q;
                if (q > dB) {
                    size_t e = fwdRel(q);
                    if (e != std::wstring::npos) {
                        if (e < in.size() &&
                            (in[e] == L')' || in[e] == L'）')) ++e;
                        dB = e;
                    }
                }
            }
        }
    }

    long long baseMid = 0;
    if (hasDate) {
        if (dayDelta >= 0) {
            baseMid = MakeEpoch(nowY, now.tm_mon + 1, now.tm_mday + (int)dayDelta, 0, 0);
        } else {
            int Y = dY;
            if (!Y) {
                Y = nowY;
                if (MakeEpoch(Y, dM, dD, 0, 0) < todayMid) ++Y;   // 过去日期 -> 明年
            }
            baseMid = MakeEpoch(Y, dM, dD, 0, 0);
        }
    }

    // ---------- 2. 时间 ----------
    bool   hasTime = false;
    size_t tA = 0, tB = 0;
    int    hF = 0, mF = 0, pfType = P_NONE;

    for (size_t i = 0; i < in.size();) {
        if (hasDate && i >= dA && i < dB) { i = dB; continue; }
        int  pft = P_NONE;
        size_t j = i;
        for (const auto& pf : kTimePf) {
            if (in.compare(i, 2, pf.tok) == 0) { pft = pf.type; j = i + 2; break; }
        }
        while (j < in.size() && in[j] == L' ') ++j;
        int h = DigitAt(in, j, 2);
        if (h < 0 || h > 23) { ++i; continue; }
        int mi; bool hm = false;
        if (j < in.size() && (in[j] == L':' || in[j] == L'：')) {
            ++j;
            int m = DigitAt(in, j, 2);
            if (m < 0 || m > 59) { ++i; continue; }
            mi = m; hm = true;
        } else if (j < in.size() && (in[j] == L'点' || in[j] == L'时')) {
            ++j;
            if (j < in.size() && in[j] == L'半') { mi = 30; ++j; hm = true; }
            else {
                size_t k = j;
                int m = DigitAt(in, k, 2);
                if (m >= 0 && m <= 59) {
                    j = k;
                    if (j < in.size() && in[j] == L'分') ++j;
                    mi = m; hm = true;
                } else {
                    mi = 0;
                }
            }
        } else {
            ++i; continue;
        }
        int hh = h;
        switch (pft) {
            case P_EARLY: if (hh == 12) hh = 0; break;
            case P_NOON:  if (hh <= 2) hh += 12; break;
            case P_PM: case P_TONIGHT: case P_TOM_NIGHT:
                if (hh < 12) hh += 12; break;
            default: break;
        }
        if (hh > 23) { ++i; continue; }
        // v0.16：时间范围「H:MM-H:MM / H:MM~H:MM / H:MM 至 H:MM」—— 起止
        // 一起消费，避免结束时间残留在任务标题里（如腾讯会议
        // 「会议时间：2026/10/14 14:30-15:30」此前识别后残留「-15:30」）
        {
            size_t k = j;
            while (k < in.size() && in[k] == L' ') ++k;
            static const wchar_t* kRangeSeps[] =
                { L"~", L"～", L"—", L"–", L"至", L"到", L"-" };
            size_t sepLen = 0;
            for (const wchar_t* sep : kRangeSeps) {
                size_t sl = wcslen(sep);
                if (in.compare(k, sl, sep) == 0) { sepLen = sl; break; }
            }
            if (sepLen) {
                size_t e = k + sepLen;
                while (e < in.size() && in[e] == L' ') ++e;
                int h2 = (e < in.size()) ? DigitAt(in, e, 2) : -1;
                if (h2 >= 0 && h2 <= 23 && e < in.size() &&
                    (in[e] == L':' || in[e] == L'：')) {
                    ++e;
                    int m2 = (e < in.size()) ? DigitAt(in, e, 2) : -1;
                    if (m2 >= 0 && m2 <= 59) j = e;   // 结束时间一并剥离（扩展消费区间）
                }
            }
        }
        tA = i; tB = j; hasTime = true;
        hF = hh; mF = mi; pfType = pft;
        break;
    }

    // ---------- 3. 合并 ----------
    long long due = 0;
    bool bHasTime = false;
    if (hasTime) {
        int Y, Mo, D;
        if (hasDate) {
            tm b = LocalTmOf(baseMid);
            Y = b.tm_year + 1900; Mo = b.tm_mon + 1; D = b.tm_mday;
        } else if (pfType == P_TONIGHT || pfType == P_TOM_NIGHT) {
            int add = (pfType == P_TOM_NIGHT) ? 1 : 0;
            Y = nowY; Mo = now.tm_mon + 1; D = now.tm_mday + add;
        } else {
            Y = nowY; Mo = now.tm_mon + 1; D = now.tm_mday;
        }
        due = MakeEpoch(Y, Mo, D, hF, mF);
        // 裸时间（无日期词、无上午下午修饰）若已过 -> 顺延到最近未来时刻
        if (!hasDate && pfType == P_NONE && due <= nowEp) {
            if (hF + 12 <= 23) {
                long long alt = MakeEpoch(Y, Mo, D, hF + 12, mF);
                if (alt > nowEp) due = alt;
                else due = MakeEpoch(Y, Mo, D + 1, hF, mF);
            } else {
                due = MakeEpoch(Y, Mo, D + 1, hF, mF);
            }
        }
        bHasTime = true;
    } else if (hasDate) {
        due = baseMid;
    }
    if (!due) return r;
    r.due = due;
    r.hasTime = bHasTime;
    r.hasDate = hasDate;   // v0.16
    r.ok = true;

    // ---------- 4. 剥离已识别词 ----------
    std::wstring out = in;
    auto cut = [&out](size_t a, size_t b) { out = out.substr(0, a) + out.substr(b); };
    if (hasDate && hasTime) {
        if (tA > dA) { cut(tA, tB); cut(dA, dB); }
        else         { cut(dA, dB); cut(tA, tB); }
    } else if (hasDate) {
        cut(dA, dB);
    } else if (hasTime) {
        cut(tA, tB);
    }
    // v0.20：日期/时间消费后可能留下空括号对（如「开会(10月21)」剥掉
    // 日期后剩「开会()」），清理掉保持标题整洁
    for (size_t p = out.find(L"()"); p != std::wstring::npos;
         p = out.find(L"()", p))
        out.erase(p, 2);
    for (size_t p = out.find(L"（）"); p != std::wstring::npos;
         p = out.find(L"（）", p))
        out.erase(p, 2);
    r.stripped = Trim(CollapseWs(out));
    return r;
}

// v0.16：多行文本逐行识别日期时间（划词添加用）—— 返回是否识别成功。
//  · 逐行 ParseWhen，取最优行：日期+时间同行(3) > 仅日期(2) > 仅时间(1)，
//    同优先级取最早的行。腾讯会议邀请函的
//    「会议时间：2026/10/14 14:30-15:30」即日期+时间同行命中；
//  · strippedOut：整体文本 —— 仅最优行剥离已识别词，其余行原样保留；
//    最优行剥离后只剩「会议时间：」这类标签残骸（整行以冒号结尾）时
//    整行去掉，空行不保留
bool ParseWhenMultiline(const std::wstring& text,
                               WhenResult& out, std::wstring& strippedOut) {
    out = WhenResult();
    strippedOut.clear();
    std::vector<std::wstring> lines;
    {
        size_t p = 0;
        for (;;) {
            size_t e = text.find(L'\n', p);
            lines.push_back(text.substr(p,
                (e == std::wstring::npos) ? std::wstring::npos : e - p));
            if (e == std::wstring::npos) break;
            p = e + 1;
        }
    }
    int    bestRank = 0;
    size_t bestLine = 0;
    WhenResult best;
    for (size_t li = 0; li < lines.size(); ++li) {
        if (lines[li].empty()) continue;
        WhenResult w = ParseWhen(lines[li]);
        if (!w.ok) continue;
        int rank = (w.hasDate ? 2 : 0) + (w.hasTime ? 1 : 0);
        if (rank > bestRank) { bestRank = rank; bestLine = li; best = w; }
    }
    if (bestRank == 0) return false;
    out = best;
    for (size_t li = 0; li < lines.size(); ++li) {
        std::wstring ln = (li == bestLine) ? Trim(best.stripped) : lines[li];
        if (li == bestLine && !ln.empty()) {
            wchar_t last = ln[ln.size() - 1];
            if (last == L'：' || last == L':') continue;   // 标签残骸整行去掉
        }
        if (ln.empty()) continue;
        if (!strippedOut.empty()) strippedOut += L'\n';
        strippedOut += ln;
    }
    return true;
}
