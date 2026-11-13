// 内存对话框模板
#include "common.h"
#include "ui/dialogs_common.h"


struct DlgBuf {
    std::vector<WORD> w;
    void Word(WORD v)   { w.push_back(v); }
    void Dword(DWORD v) { Word(LOWORD(v)); Word(HIWORD(v)); }
    void Str(const wchar_t* s) { do { Word((WORD)*s); } while (*s++); }
    void Align4()       { while (w.size() & 1) Word(0); }
};

DLGTEMPLATE* BuildDialog(const wchar_t* title, int cx, int cy,
                                const std::vector<DlgItem>& items,
                                DWORD exstyle ) {
    DlgBuf b;
    b.Dword((DWORD)(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
                    DS_SETFONT | DS_CENTER));
    b.Dword(exstyle);                    // 扩展样式（提醒弹窗 = WS_EX_TOPMOST）
    b.Word((WORD)items.size());
    b.Word(0); b.Word(0);                // x, y（DS_CENTER 忽略）
    b.Word((WORD)cx); b.Word((WORD)cy);
    b.Word(0);                           // 无菜单
    b.Word(0);                           // 窗口类
    b.Str(title);
    b.Word(9);                           // 9 pt
    b.Str(L"Microsoft YaHei UI");
    for (const DlgItem& it : items) {
        b.Align4();
        b.Dword(it.style);
        b.Dword(0);                      // 扩展样式
        b.Word(it.x); b.Word(it.y); b.Word(it.w); b.Word(it.h);
        b.Word(it.id);
        if (it.cls) {
            b.Word(0xFFFF); b.Word(it.cls);   // 预定义窗口类
        } else if (it.clsName && it.clsName[0]) {
            b.Str(it.clsName);                // 自定义类名（如日期选择器）
        } else {
            b.Word(0);
        }
        if (it.text && it.text[0]) b.Str(it.text); else b.Word(0);
        b.Word(0);                       // 无附加数据
    }
    size_t bytes = b.w.size() * sizeof(WORD);
    DLGTEMPLATE* tpl = (DLGTEMPLATE*)std::malloc(bytes);
    if (tpl) std::memcpy(tpl, b.w.data(), bytes);
    return tpl;                          // 调用者负责 free()
}
// v0.21：maxTextH —— 正文控件高度上限（px，0 = 不限）；forEdit —— 正文是
// EDIT 控件（测宽扣除边框/内边距与垂直滚动条，余量加大防内滚）
void GrowDialogToFitText(HWND h, int textId,
                                const DlgBtnIds& btnIds,
                                int maxTextH, bool forEdit) {
    HWND st = GetDlgItem(h, textId);
    if (!st || btnIds.empty()) return;
    HFONT f = (HFONT)SendMessageW(st, WM_GETFONT, 0, 0);
    RECT sr {}, cr {}, wr {}, cr0 {}, b0 {};
    HWND b0h = GetDlgItem(h, btnIds[0]);
    if (!b0h) return;
    GetWindowRect(st, &sr);
    GetClientRect(h, &cr);
    GetWindowRect(b0h, &b0);
    POINT tp = { sr.left, sr.top };
    ScreenToClient(h, &tp);
    POINT bp = { b0.left, b0.top };
    ScreenToClient(h, &bp);
    int left = tp.x, top = tp.y;
    int width = sr.right - sr.left;       // 保持模板宽度不变，只修高度
    if (forEdit)   // v0.21：EDIT 文字区比窗口窄（边框+内边距+垂直滚动条）
        width -= 8 + GetSystemMetrics(SM_CXVSCROLL);
    int oldH  = sr.bottom - sr.top;
    int gap   = bp.y - (top + oldH);      // 正文底 → 按钮顶（模板原间距）
    int btmM  = cr.bottom - (bp.y + (b0.bottom - b0.top));   // 按钮底 → 客户区底
    if (gap < 0) gap = 0;
    if (btmM < 0) btmM = 0;
    // 按实际字体在控件像素宽度内实测换行总高
    int len = GetWindowTextLengthW(st);
    std::wstring txt((size_t)(len > 0 ? len : 0) + 1, L'\0');
    if (len > 0) GetWindowTextW(st, &txt[0], len + 1);
    txt.resize((size_t)(len > 0 ? len : 0));
    HDC dc = GetDC(st);
    HGDIOBJ of = SelectObject(dc, f ? (HGDIOBJ)f
                                    : GetStockObject(DEFAULT_GUI_FONT));
    RECT calc = { 0, 0, width, 0 };
    DrawTextW(dc, txt.c_str(), -1, &calc,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(dc, of);
    ReleaseDC(st, dc);
    int textH = (calc.bottom - calc.top) + (forEdit ? 8 : 2);   // 安全余量
    if (maxTextH > 0 && textH > maxTextH)
        textH = maxTextH;                 // v0.21：上限（超出部分框内滚动）
    if (textH <= oldH) return;            // 模板估算已够，维持原样
    int newBtnTop = top + textH + gap;
    GetWindowRect(h, &wr);
    GetClientRect(h, &cr0);
    int frameW = (int)(wr.right - wr.left) - (int)(cr0.right - cr0.left);
    int frameH = (int)(wr.bottom - wr.top) - (int)(cr0.bottom - cr0.top);
    SetWindowPos(h, nullptr, 0, 0,
                 (int)(cr0.right - cr0.left) + frameW,
                 newBtnTop + (int)(b0.bottom - b0.top) + btmM + frameH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st, nullptr, 0, 0, width, textH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (int id : btnIds) {
        HWND bh = GetDlgItem(h, id);
        if (!bh) continue;
        RECT br {};
        GetWindowRect(bh, &br);
        POINT p = { br.left, br.top };
        ScreenToClient(h, &p);
        SetWindowPos(bh, nullptr, p.x, newBtnTop, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
