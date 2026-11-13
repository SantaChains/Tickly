// 时间工具
#include "common.h"
#include "core/time_util.h"
#include "core/task.h"


// ---------------------------- 时间工具 --------------------------------------
tm LocalTmOf(long long ep) {
    time_t t = (time_t)ep;
    tm l {};
    tm* p = localtime(&t);
    if (p) l = *p;
    return l;
}

long long MakeEpoch(int y, int mo, int d, int h, int mi) {
    tm t {};
    t.tm_year = y - 1900;
    t.tm_mon  = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min  = mi;
    t.tm_sec  = 0;
    t.tm_isdst = -1;
    time_t r = mktime(&t);
    return (r == (time_t)-1) ? 0 : (long long)r;
}

static bool IsLeapY(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int DaysInMonth(int y, int m) {
    static const int kD[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && IsLeapY(y)) return 29;
    return (m >= 1 && m <= 12) ? kD[m - 1] : 0;
}

long long TodayMidnight() {
    tm n = LocalTmOf((long long)time(nullptr));
    return MakeEpoch(n.tm_year + 1900, n.tm_mon + 1, n.tm_mday, 0, 0);
}

static bool IsToday(long long due) {
    if (!due) return false;
    return due >= TodayMidnight() && due < TodayMidnight() + 86400;
}

// 本周一 00:00（中国习惯：周一为一周开始）
long long ThisWeekStart() {
    tm n = LocalTmOf((long long)time(nullptr));
    long long todayMid = MakeEpoch(n.tm_year + 1900, n.tm_mon + 1, n.tm_mday, 0, 0);
    int shift = (n.tm_wday + 6) % 7;
    return todayMid - (long long)shift * 86400;
}

// 本月 1 日 00:00
long long ThisMonthStart() {
    tm n = LocalTmOf((long long)time(nullptr));
    return MakeEpoch(n.tm_year + 1900, n.tm_mon + 1, 1, 0, 0);
}

// 上周一 00:00
long long LastWeekStart() {
    return ThisWeekStart() - (long long)7 * 86400;
}

// 上月 1 日 00:00
long long LastMonthStart() {
    tm n = LocalTmOf((long long)time(nullptr));
    int y = n.tm_year + 1900, mo = n.tm_mon + 1;
    mo -= 1;
    if (mo <= 0) { mo = 12; y -= 1; }
    return MakeEpoch(y, mo, 1, 0, 0);
}

// 将 due 格式化为「今天/明天/后天/M月D日[ YYYY年]/ HH:MM」
// v0.15：逾期任务与已完成任务一律显示具体日期（M月D日[ HH:MM]，跨年含年份），
// 不再折叠成「今天 / 明天」等相对词 —— 逾期一眼看到是哪天，完成分组与
// 任务行的完成日期一致
std::wstring FormatWhen(long long due, bool hasTime, bool done,
                               bool* overdueOut) {
    if (overdueOut) *overdueOut = false;
    if (!due) return std::wstring();
    tm d = LocalTmOf(due);
    tm n = LocalTmOf((long long)time(nullptr));
    long long dueMid = MakeEpoch(d.tm_year + 1900, d.tm_mon + 1, d.tm_mday, 0, 0);
    long long nowMid = MakeEpoch(n.tm_year + 1900, n.tm_mon + 1, n.tm_mday, 0, 0);
    long long dd = (dueMid - nowMid + 43200) / 86400;

    bool od = false;
    if (!done) {
        if (hasTime) od = (due < (long long)time(nullptr));
        else         od = (dueMid < nowMid);
    }
    if (overdueOut && !done) *overdueOut = od;

    wchar_t buf[64] = L"";
    bool specific = done || od;   // v0.15：逾期 / 已完成 -> 具体日期
    if (!specific && dd == 0)      wcscpy(buf, L"今天");
    else if (!specific && dd == 1) wcscpy(buf, L"明天");
    else if (!specific && dd == 2) wcscpy(buf, L"后天");
    else if (d.tm_year == n.tm_year)
        std::swprintf(buf, 64, L"%d月%d日", d.tm_mon + 1, d.tm_mday);
    else
        std::swprintf(buf, 64, L"%d年%d月%d日", d.tm_year + 1900, d.tm_mon + 1, d.tm_mday);

    if (hasTime) {
        wchar_t tb[16];
        std::swprintf(tb, 16, L" %02d:%02d", d.tm_hour, d.tm_min);
        wcscat(buf, tb);
    }
    return buf;
}

// 任务行右侧标签文本：已完成 = 完成日期（doneAt，与完成分组、右键
//「修改完成日期」一致，v0.15）；未完成 = 格式化截止日期时间；
// 无日期且未完成 = 「＋ 日期」占位（占位与日期标签一样可点击 -> 打开
// 日期时间设置；已完成的任务不再显示占位）
std::wstring WhenLabelText(const Task& t) {
    if (t.done) return FormatWhen(t.doneAt, true, true, nullptr);
    if (t.due)  return FormatWhen(t.due, t.hasTime, false, nullptr);
    return std::wstring(L"\uFF0B 日期");
}
