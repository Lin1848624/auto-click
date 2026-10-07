// ===========================================================================
//  listview.cpp  --  深色自绘列表
//    每行：[勾选] 序号 [缩略图/坐标图标] 标题 + 副标题            [✕删除]
//    支持：点选、空格切启用、拖拽排序、滚轮、Delete 删除、方向键移动
// ===========================================================================
#include "app.h"

#include <windowsx.h>
#include <algorithm>

struct ListView {
    HWND                   hwnd   = nullptr;
    HWND                   parent = nullptr;
    int                    id     = 0;
    int                    mode   = 0;      // 0 = 找图，1 = 坐标
    std::vector<TaskItem>* items  = nullptr;
    int   sel      = -1;
    int   hot      = -1;
    int   hotPart  = 0;                     // 1 = 勾选框，2 = 删除按钮
    int   scroll   = 0;
    int   dragFrom = -1;
    int   dropTo   = -1;
    bool  dragging = false;
    bool  dragMoved = false;
    POINT dragStart{};
};

static const wchar_t* kClass = L"AutoClickList";

static int  RowStep()  { return S(48); }
static int  RowHeight(){ return S(45); }
static int  RowTop(ListView* lv, int i) { return S(4) + i * RowStep() - lv->scroll; }

static int ContentHeight(ListView* lv) {
    return S(4) * 2 + (int)lv->items->size() * RowStep();
}

static void NotifySel(ListView* lv) {
    if (lv->parent)
        SendMessageW(lv->parent, WM_COMMAND, MAKEWPARAM((WORD)lv->id, LVN_SELCHANGED),
                     (LPARAM)lv->hwnd);
}

// 内容变了（勾选 / 删除 / 重排）—— 上层收到后会立刻把配置写盘
static void NotifyChanged(ListView* lv) {
    if (lv->parent)
        SendMessageW(lv->parent, WM_COMMAND, MAKEWPARAM((WORD)lv->id, LVN_CHANGED),
                     (LPARAM)lv->hwnd);
}

static void ClampScroll(ListView* lv) {
    RECT rc;
    GetClientRect(lv->hwnd, &rc);
    const int viewH = (int)(rc.bottom - rc.top);
    const int maxScroll = (std::max)(0, ContentHeight(lv) - viewH);
    if (lv->scroll > maxScroll) lv->scroll = maxScroll;
    if (lv->scroll < 0) lv->scroll = 0;
}

static int RowAt(ListView* lv, POINT pt) {
    const int n = (int)lv->items->size();
    for (int i = 0; i < n; ++i) {
        const int top = RowTop(lv, i);
        if (pt.y >= top && pt.y < top + RowHeight()) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
static void DrawCheckbox(Gdiplus::Graphics& g, int x, int y, int sz, bool on, bool hot) {
    Gdiplus::RectF r((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)sz, (Gdiplus::REAL)sz);
    if (on) {
        UiDrawRoundRect(g, r, (Gdiplus::REAL)S(4), Theme::Accent,
                        hot ? Theme::Accent : Theme::AccentDim, 1.0f);
        Gdiplus::Pen p(Gdiplus::Color(255, 255, 255, 255), (Gdiplus::REAL)S(2));
        p.SetStartCap(Gdiplus::LineCapRound);
        p.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&p, (Gdiplus::REAL)(x + sz * 0.24f), (Gdiplus::REAL)(y + sz * 0.52f),
                       (Gdiplus::REAL)(x + sz * 0.44f), (Gdiplus::REAL)(y + sz * 0.72f));
        g.DrawLine(&p, (Gdiplus::REAL)(x + sz * 0.44f), (Gdiplus::REAL)(y + sz * 0.72f),
                       (Gdiplus::REAL)(x + sz * 0.78f), (Gdiplus::REAL)(y + sz * 0.28f));
    } else {
        UiDrawRoundRect(g, r, (Gdiplus::REAL)S(4), Theme::EditBg,
                        hot ? Theme::TextDim : Theme::Border, 1.0f);
    }
}

static void DrawClose(Gdiplus::Graphics& g, int cx, int cy, int sz, const Gdiplus::Color& col) {
    Gdiplus::Pen p(col, (Gdiplus::REAL)S(2));
    p.SetStartCap(Gdiplus::LineCapRound);
    p.SetEndCap(Gdiplus::LineCapRound);
    const Gdiplus::REAL h = (Gdiplus::REAL)sz / 2.0f;
    g.DrawLine(&p, (Gdiplus::REAL)cx - h, (Gdiplus::REAL)cy - h,
                   (Gdiplus::REAL)cx + h, (Gdiplus::REAL)cy + h);
    g.DrawLine(&p, (Gdiplus::REAL)cx + h, (Gdiplus::REAL)cy - h,
                   (Gdiplus::REAL)cx - h, (Gdiplus::REAL)cy + h);
}

static void DrawPin(Gdiplus::Graphics& g, const Gdiplus::RectF& box) {
    Gdiplus::Pen p(Theme::Accent, (Gdiplus::REAL)S(2));
    const Gdiplus::REAL cx = box.X + box.Width / 2.0f, cy = box.Y + box.Height / 2.0f;
    const Gdiplus::REAL r = (std::min)(box.Width, box.Height) / 3.0f;
    g.DrawEllipse(&p, cx - r, cy - r, r * 2, r * 2);
    g.DrawLine(&p, cx - r * 1.7f, cy, cx + r * 1.7f, cy);
    g.DrawLine(&p, cx, cy - r * 1.7f, cx, cy + r * 1.7f);
}

static void DrawRow(ListView* lv, Gdiplus::Graphics& g, int i, int top, int width) {
    const TaskItem& it = (*lv->items)[(size_t)i];
    const bool selected = (i == lv->sel);
    const bool hovered  = (i == lv->hot);
    const int  h = RowHeight();

    Gdiplus::RectF rr((Gdiplus::REAL)S(6), (Gdiplus::REAL)top,
                      (Gdiplus::REAL)(width - S(12)), (Gdiplus::REAL)h);
    Gdiplus::Color fill = Theme::ListBg;
    if (selected)                     fill = Theme::RowSel;
    else if (hovered)                 fill = Theme::RowHover;
    else if (i % 2)                   fill = Theme::RowAlt;
    UiDrawRoundRect(g, rr, (Gdiplus::REAL)S(6), fill, Gdiplus::Color(0, 0, 0, 0), 0.0f);

    if (!it.enabled) {
        // 未启用：整行压暗
        UiDrawRoundRect(g, rr, (Gdiplus::REAL)S(6), Gdiplus::Color(90, 25, 25, 29),
                        Gdiplus::Color(0, 0, 0, 0), 0.0f);
    }

    const int cy = top + h / 2;

    // 勾选框
    const int cbSz = S(17);
    DrawCheckbox(g, S(16), cy - cbSz / 2, cbSz, it.enabled,
                 hovered && lv->hotPart == 1 && i == lv->hot);

    // 序号
    UiDrawText(g, Format(L"%d", i + 1),
               Gdiplus::RectF((Gdiplus::REAL)S(42), (Gdiplus::REAL)top,
                              (Gdiplus::REAL)S(24), (Gdiplus::REAL)h),
               UiGpFont(12, false), Theme::TextDim, 2);

    // 缩略图 / 坐标图标
    const int boxX = S(74), boxW = S(46), boxH = S(32);
    Gdiplus::RectF ibox((Gdiplus::REAL)boxX, (Gdiplus::REAL)(cy - boxH / 2),
                        (Gdiplus::REAL)boxW, (Gdiplus::REAL)boxH);
    if (it.kind == ItemKind::Image) {
        UiDrawRoundRect(g, ibox, (Gdiplus::REAL)S(4), Gdiplus::Color(255, 18, 18, 22),
                        Theme::Border, 1.0f);
        Gdiplus::Bitmap* th = GetThumb(it.image, boxW - S(4), boxH - S(4));
        if (th) {
            Gdiplus::GraphicsState st = g.Save();
            Gdiplus::GraphicsPath clip;
            float rad = (float)S(4);
            Gdiplus::RectF ir(ibox.X + 1, ibox.Y + 1, ibox.Width - 2, ibox.Height - 2);
            clip.AddArc(ir.X, ir.Y, rad * 2, rad * 2, 180, 90);
            clip.AddArc(ir.X + ir.Width - rad * 2, ir.Y, rad * 2, rad * 2, 270, 90);
            clip.AddArc(ir.X + ir.Width - rad * 2, ir.Y + ir.Height - rad * 2, rad * 2, rad * 2, 0, 90);
            clip.AddArc(ir.X, ir.Y + ir.Height - rad * 2, rad * 2, rad * 2, 90, 90);
            clip.CloseFigure();
            g.SetClip(&clip);
            const Gdiplus::REAL iw = (Gdiplus::REAL)th->GetWidth();
            const Gdiplus::REAL ih = (Gdiplus::REAL)th->GetHeight();
            g.DrawImage(th, Gdiplus::RectF(ir.X + (ir.Width - iw) / 2, ir.Y + (ir.Height - ih) / 2,
                                           iw, ih));
            g.Restore(st);
        } else {
            UiDrawText(g, L"×", Gdiplus::RectF(ibox.X, ibox.Y, ibox.Width, ibox.Height),
                       UiGpFont(14, false), Theme::Danger, 1);
        }
    } else {
        UiDrawRoundRect(g, ibox, (Gdiplus::REAL)S(4), Gdiplus::Color(255, 20, 26, 36),
                        Theme::AccentDim, 1.0f);
        DrawPin(g, ibox);
    }

    // 标题 / 副标题
    const int textX = S(132);
    const int textW = width - textX - S(60);
    std::wstring title, sub;
    if (it.kind == ItemKind::Image) {
        title = FileNameOf(it.image);
        sub   = Format(L"相似度 %d%%　·　%s", it.similarity, ClickKindName(it.click).c_str());
    } else {
        title = Format(L"(%d, %d)", it.x, it.y);
        sub   = ClickKindName(it.click);
    }
    UiDrawText(g, title,
               Gdiplus::RectF((Gdiplus::REAL)textX, (Gdiplus::REAL)(top + S(6)),
                              (Gdiplus::REAL)textW, (Gdiplus::REAL)S(20)),
               UiGpFont(14, false), it.enabled ? Theme::Text : Theme::TextDim, 0);
    UiDrawText(g, sub,
               Gdiplus::RectF((Gdiplus::REAL)textX, (Gdiplus::REAL)(top + S(25)),
                              (Gdiplus::REAL)textW, (Gdiplus::REAL)S(17)),
               UiGpFont(12, false), Theme::TextDim, 0);

    // 删除按钮
    const int delCx = width - S(30);
    if (hovered && lv->hotPart == 2 && i == lv->hot) {
        UiDrawRoundRect(g, Gdiplus::RectF((Gdiplus::REAL)(delCx - S(14)), (Gdiplus::REAL)(cy - S(14)),
                                          (Gdiplus::REAL)S(28), (Gdiplus::REAL)S(28)),
                        (Gdiplus::REAL)S(6), Gdiplus::Color(70, 214, 82, 82),
                        Gdiplus::Color(0, 0, 0, 0), 0.0f);
        DrawClose(g, delCx, cy, S(12), Theme::Danger);
    } else {
        DrawClose(g, delCx, cy, S(12), Theme::TextDim);
    }
}

static void OnPaint(ListView* lv) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(lv->hwnd, &ps);
    RECT rc;
    GetClientRect(lv->hwnd, &rc);
    const int w = rc.right, hgt = rc.bottom;
    if (w <= 0 || hgt <= 0) { EndPaint(lv->hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, hgt);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        Gdiplus::SolidBrush bg(Theme::ListBg);
        g.FillRectangle(&bg, 0, 0, w, hgt);

        const int n = (int)lv->items->size();
        for (int i = 0; i < n; ++i) {
            const int top = RowTop(lv, i);
            if (top + RowHeight() < 0) continue;
            if (top > hgt) break;
            DrawRow(lv, g, i, top, w);
        }

        // 拖拽插入位置指示线
        if (lv->dragging && lv->dragMoved && lv->dropTo >= 0) {
            const int y = RowTop(lv, lv->dropTo) - S(1);
            Gdiplus::Pen pen(Theme::Accent, (Gdiplus::REAL)S(3));
            g.DrawLine(&pen, (Gdiplus::REAL)S(10), (Gdiplus::REAL)y, (Gdiplus::REAL)(w - S(10)),
                       (Gdiplus::REAL)y);
        }

        if (n == 0) {
            const std::wstring tip = lv->mode == 0
                ? L"列表还是空的 —— 点上面「截图添加」把要找的图案框下来"
                : L"列表还是空的 —— 点上面「拾取坐标」在屏幕上点一下";
            UiDrawText(g, tip, Gdiplus::RectF(0, (Gdiplus::REAL)S(60), (Gdiplus::REAL)w,
                                              (Gdiplus::REAL)S(28)),
                       UiGpFont(13, false), Theme::TextDim, 1);
        }

        // 自绘滚动条
        const int contentH = ContentHeight(lv);
        if (contentH > hgt) {
            const int trackX = w - S(9);
            const int trackW = S(5);
            Gdiplus::SolidBrush tb(Theme::Track);
            g.FillRectangle(&tb, trackX, S(4), trackW, hgt - S(8));
            const int thumbH = (std::max)(S(28), hgt * hgt / contentH);
            const int maxScroll = contentH - hgt;
            const int thumbY = S(4) + (hgt - S(8) - thumbH) * lv->scroll / (maxScroll > 0 ? maxScroll : 1);
            UiDrawRoundRect(g, Gdiplus::RectF((Gdiplus::REAL)trackX - 1.0f, (Gdiplus::REAL)thumbY,
                                              (Gdiplus::REAL)trackW + 2.0f, (Gdiplus::REAL)thumbH),
                            (Gdiplus::REAL)(trackW / 2 + 1), Theme::Thumb,
                            Gdiplus::Color(0, 0, 0, 0), 0.0f);
        }
    }

    BitBlt(dc, 0, 0, w, hgt, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(lv->hwnd, &ps);
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------
static void ToggleEnabled(ListView* lv, int i) {
    TaskItem& it = (*lv->items)[(size_t)i];
    it.enabled = !it.enabled;
    InvalidateRect(lv->hwnd, nullptr, FALSE);
    NotifyChanged(lv);
}

static void DeleteItem(ListView* lv, int i) {
    if (i < 0 || i >= (int)lv->items->size()) return;
    lv->items->erase(lv->items->begin() + i);
    if (lv->sel == i) lv->sel = -1;
    else if (lv->sel > i) --lv->sel;
    lv->hot = -1;
    lv->hotPart = 0;
    ClampScroll(lv);
    InvalidateRect(lv->hwnd, nullptr, FALSE);
    NotifyChanged(lv);
}

static void ApplyDrop(ListView* lv) {
    const int from = lv->dragFrom;
    int to = lv->dropTo;
    if (from < 0 || to < 0 || from == to) return;
    if (to > from) --to;
    if (to < 0) to = 0;
    if (to >= (int)lv->items->size()) to = (int)lv->items->size() - 1;
    if (to == from) return;
    TaskItem moved = (*lv->items)[(size_t)from];
    lv->items->erase(lv->items->begin() + from);
    lv->items->insert(lv->items->begin() + to, moved);
    lv->sel = to;
    NotifySel(lv);
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ListView* lv = (ListView*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        lv = (ListView*)cs->lpCreateParams;
        lv->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)lv);
        return TRUE;
    }
    if (!lv) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: OnPaint(lv); return 0;

        case WM_SIZE:
            ClampScroll(lv);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_MOUSEMOVE: {
            const POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            const int idx = RowAt(lv, pt);
            int part = 0;
            RECT rcCli;
            GetClientRect(hwnd, &rcCli);
            if (idx >= 0) {
                const int cy = RowTop(lv, idx) + RowHeight() / 2;
                const int cbSz = S(17);
                if (pt.x >= S(10) && pt.x <= S(16) + cbSz && std::abs(pt.y - cy) <= cbSz)
                    part = 1;
                else if (pt.x >= rcCli.right - S(48)) part = 2;
            }
            if (idx != lv->hot || part != lv->hotPart) {
                lv->hot = idx;
                lv->hotPart = part;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (!lv->dragging) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            } else if (lv->dragFrom >= 0) {
                if (std::abs(pt.y - lv->dragStart.y) > S(5)) lv->dragMoved = true;
                if (lv->dragMoved) {
                    int dst = RowAt(lv, pt);
                    if (dst < 0) dst = (pt.y < 0) ? 0 : (int)lv->items->size();
                    else if (pt.y > RowTop(lv, dst) + RowHeight() / 2) ++dst;
                    if (dst != lv->dropTo) {
                        lv->dropTo = dst;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            }
            // 拖拽到边缘时自动滚动
            if (lv->dragging && lv->dragMoved) {
                if (pt.y < S(24) && lv->scroll > 0) { lv->scroll -= S(8); ClampScroll(lv); InvalidateRect(hwnd, nullptr, FALSE); }
                RECT rcc; GetClientRect(hwnd, &rcc);
                if (pt.y > rcc.bottom - S(24)) { lv->scroll += S(8); ClampScroll(lv); InvalidateRect(hwnd, nullptr, FALSE); }
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            lv->hot = -1;
            lv->hotPart = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            const POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            const int idx = RowAt(lv, pt);
            if (idx < 0) { lv->sel = -1; InvalidateRect(hwnd, nullptr, FALSE); NotifySel(lv); return 0; }

            if (lv->hotPart == 1) { ToggleEnabled(lv, idx); return 0; }
            if (lv->hotPart == 2) { DeleteItem(lv, idx); return 0; }

            lv->sel = idx;
            lv->dragFrom = idx;
            lv->dropTo = idx;
            lv->dragging = true;
            lv->dragMoved = false;
            lv->dragStart = pt;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            NotifySel(lv);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!lv->dragging) return 0;
            lv->dragging = false;
            ReleaseCapture();
            if (lv->dragMoved) {
                ApplyDrop(lv);
                NotifyChanged(lv);
            }
            lv->dragFrom = -1;
            lv->dropTo = -1;
            lv->dragMoved = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            lv->scroll -= (delta / WHEEL_DELTA) * RowStep() * 3;
            ClampScroll(lv);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_KEYDOWN: {
            const int n = (int)lv->items->size();
            if (wp == VK_UP && n) {
                lv->sel = (lv->sel <= 0) ? 0 : lv->sel - 1;
                if (RowTop(lv, lv->sel) < 0) lv->scroll -= RowStep();
                ClampScroll(lv);
                InvalidateRect(hwnd, nullptr, FALSE);
                NotifySel(lv);
            } else if (wp == VK_DOWN && n) {
                lv->sel = (lv->sel < 0) ? 0 : (std::min)(n - 1, lv->sel + 1);
                RECT rc; GetClientRect(hwnd, &rc);
                if (RowTop(lv, lv->sel) + RowHeight() > rc.bottom) lv->scroll += RowStep();
                ClampScroll(lv);
                InvalidateRect(hwnd, nullptr, FALSE);
                NotifySel(lv);
            } else if (wp == VK_SPACE && lv->sel >= 0) {
                ToggleEnabled(lv, lv->sel);
            } else if (wp == VK_DELETE && lv->sel >= 0) {
                DeleteItem(lv, lv->sel);
            }
            return 0;
        }

        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
void ListViewRegister(HINSTANCE hInst) {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ListProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    reg = true;
}

ListView* ListViewCreate(HWND parent, int id, int mode, std::vector<TaskItem>* items) {
    ListView* lv = new ListView();
    lv->parent = parent;
    lv->id = id;
    lv->mode = mode;
    lv->items = items;
    lv->hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                               GetModuleHandleW(nullptr), lv);
    if (!lv->hwnd) { delete lv; return nullptr; }
    return lv;
}

HWND     ListViewHwnd(ListView* lv) { return lv ? lv->hwnd : nullptr; }
int      ListViewSel(ListView* lv)  { return lv ? lv->sel : -1; }
void     ListViewSetSel(ListView* lv, int idx) {
    if (!lv) return;
    lv->sel = idx;
    InvalidateRect(lv->hwnd, nullptr, FALSE);
}
void ListViewSetData(ListView* lv, int mode, std::vector<TaskItem>* items) {
    if (!lv) return;
    lv->mode = mode;
    lv->items = items;
    lv->sel = -1;
    lv->hot = -1;
    lv->hotPart = 0;
    lv->scroll = 0;
    lv->dragFrom = -1;
    lv->dropTo = -1;
    lv->dragging = false;
    lv->dragMoved = false;
    InvalidateRect(lv->hwnd, nullptr, FALSE);
}
void ListViewRefresh(ListView* lv) {
    if (!lv) return;
    if (lv->sel >= (int)lv->items->size()) lv->sel = (int)lv->items->size() - 1;
    ClampScroll(lv);
    InvalidateRect(lv->hwnd, nullptr, FALSE);
}
