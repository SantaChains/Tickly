// 划词结果气泡
#include "common.h"
#include "ui/toast.h"
#include "ui/gdi_util.h"
#include "ui/context.h"
#include "ui/theme.h"
#include "ui/tray.h"

HWND g_hwndToast = nullptr;    // 划词结果半透明气泡（同一时刻至多一个）
static BYTE g_toastAlpha = 242;       // 气泡当前整体透明度（淡出用）

// ---------------------------- 划词添加结果气泡（v1.12.0） --------------------
// 半透明置顶小窗：右下角弹出，2 秒后自动淡出消失；点击气泡打开主窗口
// v1.12.1：四角完全透明 —— 用 colorkey 抠掉圆角外区域（内容不含此色），
// 不再出现黑色四角；标题/正文字体加大；位置跟随主窗口所在显示器
static std::wstring g_toastTitle;     // 气泡标题（如「已添加到 Tickly」）
static std::wstring g_toastBody;      // 气泡正文（任务内容预览 / 失败原因）
static bool         g_toastOk = false;

static const COLORREF kToastKey = RGB(255, 0, 254);   // 透明色键（魔幻色）

LRESULT CALLBACK ToastProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER:
        if (w == 1) {                        // 1 秒到：开始淡出
            KillTimer(h, 1);
            SetTimer(h, 2, 30, nullptr);
        } else if (w == 2) {                 // 约 8 帧淡出后销毁
            g_toastAlpha = (g_toastAlpha > 32) ? (BYTE)(g_toastAlpha - 32) : 0;
            if (g_toastAlpha <= 0) {
                KillTimer(h, 2);
                DestroyWindow(h);
            } else {
                SetLayeredWindowAttributes(h, kToastKey, g_toastAlpha,
                                           LWA_COLORKEY | LWA_ALPHA);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:                     // 点击气泡 -> 打开主窗口
        ShowMainWindow();
        DestroyWindow(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;   // 背景由 WM_PAINT 统一填充（colorkey），避免四角黑块
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc {};
        GetClientRect(h, &rc);
        FillSolid(hdc, rc, kToastKey);       // 圆角外区域 = 透明色键 -> 完全透明
        // 圆角白底 + 细边框
        HGDIOBJ op = SelectObject(hdc, CachedPen(COL_BORDER));
        HGDIOBJ ob = SelectObject(hdc, CachedBrush(COL_WHITE));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, Scale(14), Scale(14));
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        // 状态圆点：成功 = 绿底白勾；失败 = 红底白叹号
        int padX = Scale(14), padY = Scale(12);
        int d    = Scale(24);
        int cx   = padX + d / 2;
        int cy   = padY + Scale(20) / 2;
        RECT cr = { cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2 };
        FillSolid(hdc, cr, g_toastOk ? COL_ACCENT : COL_OVERDUE);
        HGDIOBJ oc = SelectObject(hdc, g_penCheck);
        if (g_toastOk) {
            MoveToEx(hdc, cr.left + MulDiv(d, 24, 100),
                           cr.top  + MulDiv(d, 53, 100), nullptr);
            POINT pts[2] = {
                { cr.left + MulDiv(d, 42, 100), cr.top + MulDiv(d, 71, 100) },
                { cr.left + MulDiv(d, 77, 100), cr.top + MulDiv(d, 31, 100) },
            };
            PolylineTo(hdc, pts, 2);
        } else {
            FillSolid(hdc, { cx - Scale(1), cy - Scale(6),
                             cx + Scale(1), cy + Scale(1) }, COL_WHITE);
            FillSolid(hdc, { cx - Scale(1), cy + Scale(3),
                             cx + Scale(1), cy + Scale(5) }, COL_WHITE);
        }
        SelectObject(hdc, oc);
        // 标题 + 正文（v1.12.1 字体加大：标题 15px 加粗 / 正文 14px；
        // 正文最多 2 行，超出省略号截断）
        int tx = padX + d + Scale(12);
        HGDIOBJ tf = SelectObject(hdc, g_fontToastT);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COL_TEXT);
        RECT tr = { tx, padY, rc.right - padX, padY + Scale(20) };
        DrawTextW(hdc, g_toastTitle.c_str(), -1, &tr,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, g_fontToastB);
        SetTextColor(hdc, COL_MUTED);
        RECT br = { tx, padY + Scale(20) + Scale(6),
                    rc.right - padX, rc.bottom - padY };
        DrawTextW(hdc, g_toastBody.c_str(), -1, &br,
                  DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL | DT_NOPREFIX);
        SelectObject(hdc, tf);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        KillTimer(h, 2);
        if (g_hwndToast == h) g_hwndToast = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// 弹出气泡（同一时刻至多一个：新气泡替换旧气泡）
void ShowToast(const std::wstring& title, const std::wstring& body, bool ok) {
    if (!g_hwnd) return;
    if (g_hwndToast) { DestroyWindow(g_hwndToast); g_hwndToast = nullptr; }

    g_toastTitle = title;
    g_toastBody  = body;
    g_toastOk    = ok;

    int w    = Scale(340);
    int padX = Scale(14), padY = Scale(12);
    int d    = Scale(24);
    int textW = w - padX - d - Scale(12) - padX;
    // 量正文实际高度（限 2 行）
    int bodyH = Scale(19);
    HDC sdc = GetDC(nullptr);
    HGDIOBJ of = SelectObject(sdc, g_fontToastB);
    RECT calc = { 0, 0, textW, 0 };
    DrawTextW(sdc, body.c_str(), -1, &calc,
              DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_EDITCONTROL | DT_NOPREFIX);
    SelectObject(sdc, of);
    ReleaseDC(nullptr, sdc);
    bodyH = calc.bottom - calc.top;
    if (bodyH < Scale(19)) bodyH = Scale(19);
    if (bodyH > Scale(19) * 2) bodyH = Scale(19) * 2;
    int h = padY + Scale(20) + Scale(6) + bodyH + padY;

    // v1.13.0：显示在主窗口所在显示器工作区正中央（按需求改回居中）
    RECT wa {};
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi {};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - w) / 2;
    int y = wa.top  + ((wa.bottom - wa.top) - h) / 2;

    g_hwndToast = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"TicklyToast", L"", WS_POPUP,
        x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hwndToast) return;
    g_toastAlpha = 242;
    // v1.12.1：colorkey + 整窗半透明组合 —— 圆角外四角完全透明
    SetLayeredWindowAttributes(g_hwndToast, kToastKey, g_toastAlpha,
                               LWA_COLORKEY | LWA_ALPHA);
    ShowWindow(g_hwndToast, SW_SHOWNOACTIVATE);   // 不抢目标应用焦点
    SetTimer(g_hwndToast, 1, 1000, nullptr);      // v1.13.0：1 秒后自动淡出
}
