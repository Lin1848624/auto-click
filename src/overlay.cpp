// ===========================================================================
//  overlay.cpp  --  两个全屏覆盖层
//    ① PickRegionSavePng：冻结整个屏幕画面并压暗，拖动框选，松手即存成 PNG 模板
//    ② PickPoint：屏幕保持真实观感（几乎不可见的全屏层只负责“吃掉”这次点击），
//                 系统十字光标跟随，点一下就把坐标读出来
// ===========================================================================
#include "app.h"

#include <windowsx.h>
#include <algorithm>

bool g_picking = false;      // 覆盖层开着的时候，主窗口忽略热键与“开始”按钮

HBITMAP OverlayCreateDib(const uint32_t* px, int w, int h, int dimPercent, HDC* outMemDC);

static RECT VirtualRect() {
    RECT r;
    r.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right  = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top  + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

static RECT NormRect(POINT a, POINT b) {
    RECT r;
    r.left   = (std::min)(a.x, b.x);
    r.top    = (std::min)(a.y, b.y);
    r.right  = (std::max)(a.x, b.x);
    r.bottom = (std::max)(a.y, b.y);
    return r;
}

// ===========================================================================
//  ① 框选截图
// ===========================================================================
namespace {

struct RegionPick {
    HDC     memBright = nullptr;
    HDC     memDim    = nullptr;
    HBITMAP dibBright = nullptr;
    HBITMAP dibDim    = nullptr;
    int     w = 0, h = 0;
    bool    dragging = false;
    bool    hasSel   = false;
    POINT   p0{}, p1{};
    bool    done = false;
    bool    cancel = false;
    ScreenShot shot;
};

RegionPick g_rp;

void RegionInvalidate(HWND hwnd, const RECT* a, const RECT* b) {
    RECT u = {};
    bool any = false;
    if (a) { u = *a; any = true; }
    if (b) {
        if (any) {
            u.left   = (std::min)(u.left,   b->left);
            u.top    = (std::min)(u.top,    b->top);
            u.right  = (std::max)(u.right,  b->right);
            u.bottom = (std::max)(u.bottom, b->bottom);
        } else { u = *b; any = true; }
    }
    if (!any) return;
    InflateRect(&u, S(60), S(60));            // 给边框和尺寸标签留余量
    InvalidateRect(hwnd, &u, FALSE);
}

void RegionPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc = ps.rcPaint;
    if (rc.right <= rc.left || rc.bottom <= rc.top) { EndPaint(hwnd, &ps); return; }

    // 先铺“压暗”整屏，再把选区换回原图 —— 看起来就是只有选区是亮的
    BitBlt(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
           g_rp.memDim, rc.left, rc.top, SRCCOPY);

    const RECT sel = NormRect(g_rp.p0, g_rp.p1);
    if (g_rp.hasSel) {
        RECT isect;
        if (IntersectRect(&isect, &rc, &sel) &&
            isect.right > isect.left && isect.bottom > isect.top) {
            BitBlt(dc, isect.left, isect.top, isect.right - isect.left, isect.bottom - isect.top,
                   g_rp.memBright, isect.left, isect.top, SRCCOPY);
        }
    }

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    if (g_rp.hasSel && sel.right - sel.left >= 1 && sel.bottom - sel.top >= 1) {
        const Gdiplus::REAL x0 = (Gdiplus::REAL)sel.left + 0.5f;
        const Gdiplus::REAL y0 = (Gdiplus::REAL)sel.top + 0.5f;
        const Gdiplus::REAL ww = (Gdiplus::REAL)(sel.right - sel.left) - 1.0f;
        const Gdiplus::REAL hh = (Gdiplus::REAL)(sel.bottom - sel.top) - 1.0f;

        Gdiplus::Pen pen(Theme::Accent, (Gdiplus::REAL)S(2));
        g.DrawRectangle(&pen, x0, y0, ww, hh);

        const Gdiplus::REAL L = (Gdiplus::REAL)S(14);
        Gdiplus::Pen pen2(Gdiplus::Color(255, 255, 255, 255), (Gdiplus::REAL)S(3));
        const Gdiplus::REAL xs[2] = { x0, x0 + ww };
        const Gdiplus::REAL ys[2] = { y0, y0 + hh };
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                const Gdiplus::REAL px = xs[i], py = ys[j];
                g.DrawLine(&pen2, px, py, px + (i ? -L : L), py);
                g.DrawLine(&pen2, px, py, px, py + (j ? -L : L));
            }
        }

        // 尺寸标签贴在选区外侧
        const std::wstring lab = Format(L"%d × %d", sel.right - sel.left, sel.bottom - sel.top);
        Gdiplus::Font* f = UiGpFont(13, true);
        Gdiplus::RectF box;
        g.MeasureString(lab.c_str(), (INT)lab.size(), f, Gdiplus::RectF(0, 0, 600, 80), &box);
        const Gdiplus::REAL bw = box.Width + S(18), bh = box.Height + S(8);
        Gdiplus::REAL bx = x0;
        Gdiplus::REAL by = (Gdiplus::REAL)sel.bottom + S(6);
        if (by + bh > (Gdiplus::REAL)g_rp.h) by = (Gdiplus::REAL)sel.top - bh - S(6);
        if (by < 0) by = (Gdiplus::REAL)sel.bottom - bh - S(6);
        if (bx + bw > (Gdiplus::REAL)g_rp.w) bx = (Gdiplus::REAL)g_rp.w - bw;
        if (bx < 0) bx = 0;
        UiDrawRoundRect(g, Gdiplus::RectF(bx, by, bw, bh), (Gdiplus::REAL)S(4),
                        Theme::Accent, Gdiplus::Color(0, 0, 0, 0), 0.0f);
        UiDrawText(g, lab, Gdiplus::RectF(bx, by, bw, bh), f,
                   Gdiplus::Color(255, 255, 255, 255), 1);
    } else {
        // 还没开始框选时的操作提示，固定在主屏顶部居中
        const std::wstring tip = L"拖动鼠标框选要找图的目标区域　·　按 ESC 取消";
        Gdiplus::Font* f = UiGpFont(15, false);
        Gdiplus::RectF box;
        g.MeasureString(tip.c_str(), (INT)tip.size(), f, Gdiplus::RectF(0, 0, 1200, 80), &box);
        const Gdiplus::REAL pw = box.Width + S(40), ph = box.Height + S(22);
        const Gdiplus::REAL primaryLeft = (Gdiplus::REAL)(-VirtualRect().left);
        const Gdiplus::REAL primaryTop  = (Gdiplus::REAL)(-VirtualRect().top);
        Gdiplus::REAL px = primaryLeft + ((Gdiplus::REAL)GetSystemMetrics(SM_CXSCREEN) - pw) / 2.0f;
        Gdiplus::REAL py = primaryTop + S(64);
        UiDrawRoundRect(g, Gdiplus::RectF(px, py, pw, ph), (Gdiplus::REAL)S(6),
                        Gdiplus::Color(238, 20, 20, 26), Gdiplus::Color(255, 70, 70, 84), 1.0f);
        UiDrawText(g, tip, Gdiplus::RectF(px, py, pw, ph), f,
                   Gdiplus::Color(255, 235, 235, 240), 1);
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK RegionProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: RegionPaint(hwnd); return 0;
        case WM_DPICHANGED: return 0;                 // 横跨多屏时不要被系统改尺寸
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_CROSS)); return TRUE;
        case WM_LBUTTONDOWN: {
            const RECT old = NormRect(g_rp.p0, g_rp.p1);
            g_rp.dragging = true;
            g_rp.hasSel = true;
            g_rp.p0.x = g_rp.p1.x = GET_X_LPARAM(lp);
            g_rp.p0.y = g_rp.p1.y = GET_Y_LPARAM(lp);
            SetCapture(hwnd);
            RegionInvalidate(hwnd, &old, nullptr);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!g_rp.dragging) return 0;
            const RECT old = NormRect(g_rp.p0, g_rp.p1);
            g_rp.p1.x = GET_X_LPARAM(lp);
            g_rp.p1.y = GET_Y_LPARAM(lp);
            const RECT nw = NormRect(g_rp.p0, g_rp.p1);
            RegionInvalidate(hwnd, &old, &nw);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!g_rp.dragging) return 0;
            g_rp.dragging = false;
            ReleaseCapture();
            g_rp.p1.x = GET_X_LPARAM(lp);
            g_rp.p1.y = GET_Y_LPARAM(lp);
            const RECT sel = NormRect(g_rp.p0, g_rp.p1);
            if (sel.right - sel.left < 4 || sel.bottom - sel.top < 4) {
                g_rp.hasSel = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            g_rp.done = true;
            return 0;
        }
        case WM_RBUTTONDOWN:
            g_rp.cancel = true; g_rp.done = true; return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { g_rp.cancel = true; g_rp.done = true; }
            else if (wp == VK_RETURN && g_rp.hasSel) { g_rp.done = true; }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool PickRegionSavePng(HWND owner, std::wstring& outPath) {
    if (!CaptureScreen(g_rp.shot)) return false;

    g_rp.w = g_rp.shot.w;
    g_rp.h = g_rp.shot.h;
    g_rp.dibBright = OverlayCreateDib(g_rp.shot.px.data(), g_rp.w, g_rp.h, 0,  &g_rp.memBright);
    g_rp.dibDim    = OverlayCreateDib(g_rp.shot.px.data(), g_rp.w, g_rp.h, 58, &g_rp.memDim);
    if (!g_rp.dibBright || !g_rp.dibDim) {
        if (g_rp.dibBright) DeleteObject(g_rp.dibBright);
        if (g_rp.dibDim)    DeleteObject(g_rp.dibDim);
        if (g_rp.memBright) DeleteDC(g_rp.memBright);
        if (g_rp.memDim)    DeleteDC(g_rp.memDim);
        g_rp = RegionPick();
        return false;
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = RegionProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = L"AutoClickRegionPick";
        RegisterClassExW(&wc);
        reg = true;
    }

    const RECT vr = VirtualRect();
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                L"AutoClickRegionPick", L"", WS_POPUP,
                                vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
                                owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        DeleteObject(g_rp.dibBright); DeleteObject(g_rp.dibDim);
        DeleteDC(g_rp.memBright); DeleteDC(g_rp.memDim);
        g_rp = RegionPick();
        return false;
    }

    g_picking = true;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg;
    while (!g_rp.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    const RECT sel = NormRect(g_rp.p0, g_rp.p1);
    bool ok = false;
    if (!g_rp.cancel && g_rp.hasSel && sel.right > sel.left && sel.bottom > sel.top) {
        const int cw = sel.right - sel.left, ch = sel.bottom - sel.top;
        std::vector<uint32_t> crop((size_t)cw * ch);
        for (int y = 0; y < ch; ++y) {
            memcpy(crop.data() + (size_t)y * cw,
                   g_rp.shot.px.data() + (size_t)(sel.top + y) * g_rp.w + sel.left,
                   (size_t)cw * 4);
        }
        EnsureDir(DataDir());
        EnsureDir(ImagesDir());
        outPath = ImagesDir() + L"\\img_" + NowStamp() + L".png";
        ok = SavePngRgba(crop.data(), cw, ch, outPath);
    }

    DestroyWindow(hwnd);
    DeleteObject(g_rp.dibBright);
    DeleteObject(g_rp.dibDim);
    DeleteDC(g_rp.memBright);
    DeleteDC(g_rp.memDim);
    g_rp = RegionPick();
    g_picking = false;
    return ok;
}

// ===========================================================================
//  ② 坐标拾取
// ===========================================================================
namespace {

struct PointPick {
    HWND  full = nullptr;
    HWND  hud  = nullptr;
    POINT pt{};
    bool  done = false;
    bool  cancel = false;
    int   hudW = 0, hudH = 0;
};

PointPick g_pp;

void HudRepaint(HWND hud) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hud, &ps);
    RECT rc;
    GetClientRect(hud, &rc);

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    UiDrawRoundRect(g, Gdiplus::RectF(0.5f, 0.5f, (Gdiplus::REAL)rc.right - 1.0f,
                                      (Gdiplus::REAL)rc.bottom - 1.0f),
                    (Gdiplus::REAL)S(8),
                    Gdiplus::Color(240, 22, 22, 28), Theme::Accent, 1.0f);

    const std::wstring l1 = Format(L"X = %d    Y = %d", g_pp.pt.x, g_pp.pt.y);
    const std::wstring l2 = L"左键确认　·　右键 / ESC 取消";
    UiDrawText(g, l1, Gdiplus::RectF((Gdiplus::REAL)S(12), (Gdiplus::REAL)S(6),
                                     (Gdiplus::REAL)rc.right - S(24), (Gdiplus::REAL)S(28)),
               UiGpFont(16, true), Gdiplus::Color(255, 255, 255, 255), 1);
    UiDrawText(g, l2, Gdiplus::RectF((Gdiplus::REAL)S(12), (Gdiplus::REAL)S(33),
                                     (Gdiplus::REAL)rc.right - S(24), (Gdiplus::REAL)S(18)),
               UiGpFont(12, false), Theme::TextDim, 1);
    EndPaint(hud, &ps);
}

LRESULT CALLBACK HudProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: HudRepaint(hwnd); return 0;
        case WM_NCHITTEST: return HTTRANSPARENT;     // 鼠标穿透，别挡住拾取
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 提示条自动躲开鼠标：鼠标在下半屏就贴顶，反之贴底
void HudFollow() {
    if (!g_pp.hud) return;
    const RECT vr = VirtualRect();
    const int midY = vr.top + (vr.bottom - vr.top) / 2;
    int y = (g_pp.pt.y > midY) ? (vr.top + S(40)) : (vr.bottom - S(40) - g_pp.hudH);
    int x = g_pp.pt.x - g_pp.hudW / 2;
    if (x < vr.left + S(8)) x = vr.left + S(8);
    if (x + g_pp.hudW > vr.right - S(8)) x = vr.right - S(8) - g_pp.hudW;
    SetWindowPos(g_pp.hud, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(g_pp.hud, nullptr, FALSE);
}

LRESULT CALLBACK FullProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(RGB(20, 20, 24));
            FillRect(dc, &rc, b);
            DeleteObject(b);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DPICHANGED: return 0;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_CROSS)); return TRUE;
        case WM_MOUSEMOVE:
            GetCursorPos(&g_pp.pt);
            HudFollow();
            return 0;
        case WM_LBUTTONDOWN:
            GetCursorPos(&g_pp.pt);
            g_pp.done = true;
            return 0;
        case WM_RBUTTONDOWN:
            g_pp.cancel = true; g_pp.done = true; return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { g_pp.cancel = true; g_pp.done = true; }
            else if (wp == VK_F2) { GetCursorPos(&g_pp.pt); g_pp.done = true; }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool PickPoint(HWND owner, POINT& out) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = FullProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = L"AutoClickPointPickFull";
        RegisterClassExW(&wc);

        WNDCLASSEXW hc = {};
        hc.cbSize = sizeof(hc);
        hc.lpfnWndProc = HudProc;
        hc.hInstance = GetModuleHandleW(nullptr);
        hc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        hc.lpszClassName = L"AutoClickPointPickHud";
        RegisterClassExW(&hc);
        reg = true;
    }

    const RECT vr = VirtualRect();
    g_pp = PointPick();
    g_pp.hudW = S(232);
    g_pp.hudH = S(56);

    // 全屏窗口：alpha 极低（肉眼几乎不可见），但会吃掉这次点击、并接收鼠标移动
    g_pp.full = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                L"AutoClickPointPickFull", L"", WS_POPUP,
                                vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
                                owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_pp.full) return false;
    SetLayeredWindowAttributes(g_pp.full, 0, 1, LWA_ALPHA);

    g_pp.hud = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                               WS_EX_TRANSPARENT,
                               L"AutoClickPointPickHud", L"", WS_POPUP,
                               0, 0, g_pp.hudW, g_pp.hudH,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_pp.hud) {
        HRGN rgn = CreateRoundRectRgn(0, 0, g_pp.hudW + 1, g_pp.hudH + 1, S(16), S(16));
        SetWindowRgn(g_pp.hud, rgn, TRUE);
    }

    GetCursorPos(&g_pp.pt);
    g_picking = true;
    ShowWindow(g_pp.full, SW_SHOW);
    SetForegroundWindow(g_pp.full);
    SetFocus(g_pp.full);
    if (g_pp.hud) { ShowWindow(g_pp.hud, SW_SHOWNOACTIVATE); HudFollow(); }

    MSG msg;
    while (!g_pp.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    const bool ok = !g_pp.cancel;
    if (ok) out = g_pp.pt;

    if (g_pp.hud)  DestroyWindow(g_pp.hud);
    if (g_pp.full) DestroyWindow(g_pp.full);
    g_pp = PointPick();
    g_picking = false;
    return ok;
}
