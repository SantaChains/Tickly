// DPI 感知
#include "common.h"
#include "platform/dpi.h"


// ---------------------------- DPI 感知 --------------------------------------
// v2.0：进程必须声明 DPI 感知，否则在 125%/150%/175% 这类非 100% 缩放的
// 屏幕上，Windows 会先把整个窗口按 96 DPI 画好再做整幅位图拉伸，文字边界
// 被插值模糊 —— 这就是「打包版文字发虚」的根因（GDI 字体本身没有问题）。
// 正常路径由内嵌清单 app.manifest 声明（PerMonitorV2）；下面这段是运行时兜底，
// 清单丢失/被裁剪时仍能拿到清晰文字。
void EnableDpiAwareness() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL (WINAPI *PFN_SetDpiCtx)(HANDLE);
        auto setCtx = (PFN_SetDpiCtx)GetProcAddress(u32,
                                       "SetProcessDpiAwarenessContext");
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4，老版 PM = -3
        if (setCtx && setCtx((HANDLE)(LONG_PTR)-4)) return;
        if (setCtx && setCtx((HANDLE)(LONG_PTR)-3)) return;
    }
    SetProcessDPIAware();   // Win7+ 系统级感知；清单已生效时调用失败，无害
}
