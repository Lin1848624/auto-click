// ===========================================================================
//  main.cpp  --  主窗口：标签页、工具条、列表、参数区、运行控制
// ===========================================================================
#include "app.h"

#include <commdlg.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <algorithm>
#include <map>
#include <string>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

AppState g_app;

// 客户区逻辑尺寸
static const int CW = 900;
static const int CH = 600;

static HINSTANCE g_hInst = nullptr;
static HWND      g_hMain = nullptr;
static HBRUSH    g_brBg = nullptr;
static HBRUSH    g_brEdit = nullptr;
static ListView* g_list = nullptr;
static std::wstring g_statusShown;

// 控件句柄
struct Ui {
    HWND tabImage = nullptr, tabPoint = nullptr;
    HWND btnA = nullptr, btnB = nullptr;                 // 随模式切换的两个主按钮
    HWND btnUp = nullptr, btnDown = nullptr, btnDel = nullptr, btnClear = nullptr;

    HWND btnClickMode = nullptr;
    HWND lblSim = nullptr, btnSimMinus = nullptr, lblSimVal = nullptr, btnSimPlus = nullptr;
    HWND lblX = nullptr, editX = nullptr, lblY = nullptr, editY = nullptr;
    HWND lblItemHint = nullptr;

    HWND lblStep = nullptr, editStep = nullptr, lblStepU = nullptr;
    HWND lblLoop = nullptr, editLoop = nullptr, lblLoopU = nullptr;
    HWND lblStartD = nullptr, editStartD = nullptr, lblStartDU = nullptr;

    HWND lblNotFound = nullptr, segSkip = nullptr, segRetry = nullptr, segStop = nullptr;
    HWND lblRetry = nullptr, editRetry = nullptr, lblRetryU = nullptr;
    HWND lblRetryIv = nullptr, editRetryIv = nullptr, lblRetryIvU = nullptr;
    HWND lblScope = nullptr, segScopeCur = nullptr, segScopeBoth = nullptr;

    HWND status = nullptr, btnStart = nullptr, btnStop = nullptr, lblHint = nullptr;
    HWND list = nullptr;
};
static Ui ui;

// ---------------------------------------------------------------------------
// 自绘按钮（自定义窗口类，比 BS_OWNERDRAW + 子类化省事）
// ---------------------------------------------------------------------------
enum BtnStyle { BS_Normal = 0, BS_Primary = 1, BS_Danger = 2, BS_Seg = 3, BS_Tab = 4 };

struct BtnData {
    int  style = BS_Normal;
    bool hover = false, pressed = false, active = false, disabled = false;
};

static std::map<HWND, BtnData> g_btnData;

static void BtnPaint(HWND h) {
    auto it = g_btnData.find(h);
    if (it == g_btnData.end()) return;
    BtnData& d = it->second;

    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    const int w = rc.right, hh = rc.bottom;
    if (w <= 0 || hh <= 0) { EndPaint(h, &ps); return; }

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, hh);
    HGDIOBJ old = SelectObject(mem, bmp);

    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        wchar_t text[128] = {};
        GetWindowTextW(h, text, 128);

        Gdiplus::Color fill, border, tcol = Theme::Text;
        const float rad = (d.style == BS_Tab) ? (float)S(6) : (float)S(6);

        switch (d.style) {
            case BS_Primary:
                fill = d.disabled ? Gdiplus::Color(255, 52, 74, 110)
                                  : (d.pressed ? Gdiplus::Color(255, 40, 100, 200)
                                               : (d.hover ? Theme::Accent : Gdiplus::Color(255, 48, 112, 218)));
                border = Gdiplus::Color(0, 0, 0, 0);
                tcol = d.disabled ? Theme::TextDim : Gdiplus::Color(255, 255, 255, 255);
                break;
            case BS_Danger:
                fill = d.pressed ? Gdiplus::Color(255, 150, 52, 52)
                                 : (d.hover ? Gdiplus::Color(255, 120, 46, 46) : Theme::EditBg);
                border = d.hover ? Gdiplus::Color(0, 0, 0, 0) : Theme::Border;
                tcol = d.hover ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 226, 140, 140);
                break;
            case BS_Seg:
            case BS_Tab:
                if (d.active) {
                    fill = Theme::Accent;
                    border = Gdiplus::Color(0, 0, 0, 0);
                    tcol = Gdiplus::Color(255, 255, 255, 255);
                } else {
                    fill = d.pressed ? Gdiplus::Color(255, 34, 34, 40)
                                     : (d.hover ? Theme::RowHover : Gdiplus::Color(255, 36, 36, 42));
                    border = Theme::Border;
                    tcol = d.hover ? Theme::Text : Theme::TextDim;
                }
                break;
            default:
                fill = d.pressed ? Gdiplus::Color(255, 34, 34, 40)
                                 : (d.hover ? Theme::RowHover : Theme::EditBg);
                border = d.hover ? Theme::AccentDim : Theme::Border;
                tcol = d.disabled ? Theme::TextDim : Theme::Text;
                break;
        }
        if (d.disabled && d.style != BS_Primary) tcol = Gdiplus::Color(255, 96, 96, 104);

        UiDrawRoundRect(g, Gdiplus::RectF(0.5f, 0.5f, (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)hh - 1.0f),
                        rad, fill, border, 1.0f);
        UiDrawText(g, text, Gdiplus::RectF(0, 0, (Gdiplus::REAL)w, (Gdiplus::REAL)hh),
                   UiGpFont(d.style == BS_Tab ? 15 : 14, d.style == BS_Tab || d.style == BS_Primary), tcol, 1);
    }

    BitBlt(dc, 0, 0, w, hh, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK BtnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto it = g_btnData.find(h);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: BtnPaint(h); return 0;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE;
        case WM_MOUSEMOVE:
            if (it != g_btnData.end() && !it->second.hover) {
                it->second.hover = true;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (it != g_btnData.end()) {
                it->second.hover = false;
                it->second.pressed = false;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (it != g_btnData.end() && !it->second.disabled) {
                it->second.pressed = true;
                SetCapture(h);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            if (it == g_btnData.end()) return 0;
            const bool was = it->second.pressed;
            it->second.pressed = false;
            ReleaseCapture();
            InvalidateRect(h, nullptr, FALSE);
            RECT rc;
            GetClientRect(h, &rc);
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (was && !it->second.disabled && PtInRect(&rc, pt)) {
                HWND parent = GetParent(h);
                int id = GetDlgCtrlID(h);
                SendMessageW(parent, WM_COMMAND, MAKEWPARAM((WORD)id, 0), (LPARAM)h);
            }
            return 0;
        }
        case WM_SETFOCUS: return 0;      // 别抢键盘焦点（列表要用方向键）
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void RegisterBtnClass() {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BtnProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = L"AutoClickBtn";
    RegisterClassExW(&wc);
    reg = true;
}

static HWND MakeBtn(HWND parent, int id, const wchar_t* text, BtnStyle style) {
    HWND h = CreateWindowExW(0, L"AutoClickBtn", text, WS_CHILD | WS_VISIBLE,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    g_btnData[h].style = style;
    return h;
}

static void BtnSetActive(HWND h, bool on) {
    auto it = g_btnData.find(h);
    if (it == g_btnData.end() || it->second.active == on) return;
    it->second.active = on;
    InvalidateRect(h, nullptr, FALSE);
}

static void BtnSetDisabled(HWND h, bool dis) {
    auto it = g_btnData.find(h);
    if (it == g_btnData.end() || it->second.disabled == dis) return;
    it->second.disabled = dis;
    InvalidateRect(h, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------
static void Place(HWND& h, int x, int y, int w, int ht, bool visible = true) {
    if (!h) return;
    MoveWindow(h, S(x), S(y), S(w), S(ht), TRUE);
    ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
}

static void PlaceLabel(HWND& h, const wchar_t* text, int x, int y, int w, int ht,
                       bool visible = true) {
    if (!h) {
        h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                            S(x), S(y), S(w), S(ht), g_hMain, nullptr, g_hInst, nullptr);
    }
    MoveWindow(h, S(x), S(y), S(w), S(ht), TRUE);
    ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
}

static void PlaceEdit(HWND& h, int id, int x, int y, int w, int ht, bool visible = true) {
    if (!h) {
        h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL |
                            ES_NUMBER | ES_CENTER, S(x), S(y), S(w), S(ht),
                            g_hMain, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(4), S(4)));
    }
    MoveWindow(h, S(x), S(y), S(w), S(ht), TRUE);
    ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
}

static void SetEditInt(HWND h, int v) {
    wchar_t buf[32];
    wsprintfW(buf, L"%d", v);
    SetWindowTextW(h, buf);
}

static int GetEditInt(HWND h, int def, int lo, int hi) {
    wchar_t buf[32] = {};
    GetWindowTextW(h, buf, 32);
    int v = buf[0] ? _wtoi(buf) : def;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static HFONT FontBtn()  { return UiFont(14, false); }
static HFONT FontLbl()  { return UiFont(13, false); }
static HFONT FontEdit() { return UiFont(13, false); }

static void ApplyFonts() {
    SendMessageW(ui.tabImage, WM_SETFONT, (WPARAM)UiFont(15, true), TRUE);
    SendMessageW(ui.tabPoint, WM_SETFONT, (WPARAM)UiFont(15, true), TRUE);
    HWND lbls[] = { ui.lblSim, ui.lblX, ui.lblY, ui.lblItemHint, ui.lblStep, ui.lblStepU,
                    ui.lblLoop, ui.lblLoopU, ui.lblStartD, ui.lblStartDU, ui.lblNotFound,
                    ui.lblRetry, ui.lblRetryU, ui.lblRetryIv, ui.lblRetryIvU, ui.lblScope,
                    ui.lblHint, ui.lblSimVal };
    for (HWND h : lbls) if (h) SendMessageW(h, WM_SETFONT, (WPARAM)FontLbl(), TRUE);
    HWND edits[] = { ui.editX, ui.editY, ui.editStep, ui.editLoop, ui.editStartD,
                     ui.editRetry, ui.editRetryIv };
    for (HWND h : edits) if (h) SendMessageW(h, WM_SETFONT, (WPARAM)FontEdit(), TRUE);
    if (ui.status) SendMessageW(ui.status, WM_SETFONT, (WPARAM)UiFont(13, false), TRUE);
    for (auto& kv : g_btnData) SendMessageW(kv.first, WM_SETFONT, (WPARAM)FontBtn(), TRUE);
}

static void Layout() {
    const bool imgTab = (g_app.tab == 0);
    const int sel = ListViewSel(g_list);
    const bool hasSel = (sel >= 0 && sel < (int)g_app.cur().size());

    Place(ui.tabImage, 18, 14, 132, 36);
    Place(ui.tabPoint, 158, 14, 132, 36);

    // 工具条：左边两个按钮随模式换文字和宽度，右边四个是共用的
    Place(ui.btnA, 18, 64, 124, 32);
    Place(ui.btnB, 152, 64, imgTab ? 138 : 170, 32, true);
    Place(ui.btnUp,    558, 64, 36, 32);
    Place(ui.btnDown,  600, 64, 36, 32);
    Place(ui.btnDel,   646, 64, 84, 32);
    Place(ui.btnClear, 738, 64, 84, 32);

    Place(ui.list, 18, 108, CW - 36, 284);

    // 选中项参数行
    Place(ui.btnClickMode, 18, 402, 172, 30, hasSel);
    PlaceLabel(ui.lblSim, L"相似度", 204, 402, 58, 30, hasSel && imgTab);
    Place(ui.btnSimMinus,  262, 402, 32, 30, hasSel && imgTab);
    Place(ui.lblSimVal,    298, 402, 56, 30, hasSel && imgTab);
    Place(ui.btnSimPlus,   358, 402, 32, 30, hasSel && imgTab);
    PlaceLabel(ui.lblX, L"X", 412, 402, 18, 30, hasSel && !imgTab);
    PlaceEdit(ui.editX, IDC_EDIT_X, 432, 402, 68, 30, hasSel && !imgTab);
    PlaceLabel(ui.lblY, L"Y", 512, 402, 18, 30, hasSel && !imgTab);
    PlaceEdit(ui.editY, IDC_EDIT_Y, 532, 402, 68, 30, hasSel && !imgTab);
    PlaceLabel(ui.lblItemHint,
               L"选中列表里的一项，就能单独改它的点击方式；找图项还可以单独调相似度",
               18, 404, CW - 36, 26, !hasSel);

    // 运行参数
    PlaceLabel(ui.lblStep,   L"每步间隔", 18, 448, 62, 26);
    PlaceEdit(ui.editStep,   IDC_EDIT_STEPDELAY, 84, 447, 54, 27);
    PlaceLabel(ui.lblStepU,  L"毫秒", 142, 448, 34, 26);
    PlaceLabel(ui.lblLoop,   L"循环", 190, 448, 34, 26);
    PlaceEdit(ui.editLoop,   IDC_EDIT_LOOP, 226, 447, 48, 27);
    PlaceLabel(ui.lblLoopU,  L"次（0 = 一直循环）", 280, 448, 150, 26);
    PlaceLabel(ui.lblStartD, L"开始前延时", 442, 448, 70, 26);
    PlaceEdit(ui.editStartD, IDC_EDIT_STARTDELAY, 516, 447, 54, 27);
    PlaceLabel(ui.lblStartDU,L"毫秒", 574, 448, 34, 26);

    PlaceLabel(ui.lblNotFound, L"找不到时", 18, 486, 58, 26);
    Place(ui.segSkip,  80, 484, 74, 28);
    Place(ui.segRetry, 158, 484, 74, 28);
    Place(ui.segStop,  236, 484, 74, 28);
    PlaceLabel(ui.lblRetry, L"重试", 322, 486, 34, 26);
    PlaceEdit(ui.editRetry, IDC_EDIT_RETRY, 358, 483, 44, 27);
    PlaceLabel(ui.lblRetryU, L"次，", 406, 486, 28, 26);
    PlaceLabel(ui.lblRetryIv, L"每次等", 434, 486, 52, 26);
    PlaceEdit(ui.editRetryIv, IDC_EDIT_RETRYIV, 490, 483, 54, 27);
    PlaceLabel(ui.lblRetryIvU, L"毫秒", 548, 486, 34, 26);
    PlaceLabel(ui.lblScope, L"执行范围", 596, 486, 62, 26);
    Place(ui.segScopeCur,  662, 484, 100, 28);
    Place(ui.segScopeBoth, 766, 484, 100, 28);

    Place(ui.status, 18, 522, 540, 26);
    Place(ui.btnStop,  574, 516, 104, 40);
    Place(ui.btnStart, 726, 516, 156, 40);
    PlaceLabel(ui.lblHint, L"F6 开始 / 停止　·　F12 紧急停止　·　F7 记住鼠标当前位置　·　运行时本窗口会自动最小化",
               18, 570, CW - 36, 22);

    ApplyFonts();
    InvalidateRect(g_hMain, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// 参数区 / 状态刷新
// ---------------------------------------------------------------------------
static void SyncItemRow() {
    const int sel = ListViewSel(g_list);
    std::vector<TaskItem>& v = g_app.cur();
    if (sel < 0 || sel >= (int)v.size()) { Layout(); return; }
    TaskItem& it = v[(size_t)sel];

    wchar_t buf[128];
    wsprintfW(buf, L"点击方式：%s", ClickKindName(it.click).c_str());
    SetWindowTextW(ui.btnClickMode, buf);
    BtnSetDisabled(ui.btnClickMode, false);

    wsprintfW(buf, L"%d%%", it.similarity);
    SetWindowTextW(ui.lblSimVal, buf);
    BtnSetDisabled(ui.btnSimMinus, it.similarity <= 50);
    BtnSetDisabled(ui.btnSimPlus,  it.similarity >= 100);

    SetEditInt(ui.editX, it.x);
    SetEditInt(ui.editY, it.y);
    Layout();
}

static void SyncRunRow() {
    SetEditInt(ui.editStep,   g_app.run.stepDelayMs);
    SetEditInt(ui.editLoop,   g_app.run.loopCount);
    SetEditInt(ui.editStartD, g_app.run.startDelayMs);
    SetEditInt(ui.editRetry,  g_app.run.retryCount);
    SetEditInt(ui.editRetryIv, g_app.run.retryIntervalMs);
    BtnSetActive(ui.segSkip,  g_app.run.onNotFound == NotFoundAction::Skip);
    BtnSetActive(ui.segRetry, g_app.run.onNotFound == NotFoundAction::Retry);
    BtnSetActive(ui.segStop,  g_app.run.onNotFound == NotFoundAction::Stop);
    BtnSetActive(ui.segScopeCur,  g_app.scopeBoth == 0);
    BtnSetActive(ui.segScopeBoth, g_app.scopeBoth == 1);
    const bool retry = (g_app.run.onNotFound == NotFoundAction::Retry);
    EnableWindow(ui.editRetry, retry);
    EnableWindow(ui.editRetryIv, retry);
}

static void PullRunRow() {
    g_app.run.stepDelayMs    = GetEditInt(ui.editStep, 500, 0, 60000);
    g_app.run.loopCount      = GetEditInt(ui.editLoop, 1, 0, 100000);
    g_app.run.startDelayMs   = GetEditInt(ui.editStartD, 1000, 0, 60000);
    g_app.run.retryCount     = GetEditInt(ui.editRetry, 10, 0, 999);
    g_app.run.retryIntervalMs = GetEditInt(ui.editRetryIv, 300, 0, 60000);
}

static void SetStatusText(const std::wstring& s) {
    if (s == g_statusShown) return;
    g_statusShown = s;
    // 文字同时写进控件，外部工具（无障碍 / 自动化测试）才读得到
    if (ui.status) SetWindowTextW(ui.status, s.c_str());
    if (ui.status) InvalidateRect(ui.status, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// 模式切换
// ---------------------------------------------------------------------------
static void ApplyTab() {
    const bool imgTab = (g_app.tab == 0);
    BtnSetActive(ui.tabImage, imgTab);
    BtnSetActive(ui.tabPoint, !imgTab);
    SetWindowTextW(ui.btnA, imgTab ? L"截图添加" : L"拾取坐标");
    SetWindowTextW(ui.btnB, imgTab ? L"从文件添加" : L"记当前位置 F7");
    ListViewSetSel(g_list, -1);
    if (g_picking) return;
    SyncItemRow();
}

// ---------------------------------------------------------------------------
// 执行控制
// ---------------------------------------------------------------------------
static void RestoreMainWindow() {
    if (IsIconic(g_hMain)) ShowWindow(g_hMain, SW_RESTORE);
    SetForegroundWindow(g_hMain);
}

static void StartRun() {
    if (g_picking || EngineRunning()) return;
    PullRunRow();
    SaveConfig();

    std::vector<TaskItem> list;
    if (g_app.scopeBoth) {
        list.insert(list.end(), g_app.images.begin(), g_app.images.end());
        list.insert(list.end(), g_app.points.begin(), g_app.points.end());
    } else {
        list = g_app.cur();
    }

    bool anyEnabled = false;
    for (const TaskItem& it : list) if (it.enabled) anyEnabled = true;
    if (!anyEnabled) {
        SetStatusText(L"没有可执行的步骤 —— 请先添加内容，并确认左边的勾选框是勾上的");
        return;
    }

    ShowWindow(g_hMain, SW_MINIMIZE);
    if (!EngineStart(g_hMain, list, g_app.run)) {
        RestoreMainWindow();
        SetStatusText(L"启动失败：上一轮还没结束");
        return;
    }
    BtnSetDisabled(ui.btnStart, true);
    BtnSetDisabled(ui.btnStop, false);
}

static void StopRun() {
    if (EngineRunning()) EngineStop();
}

// ---------------------------------------------------------------------------
// 添加项
// ---------------------------------------------------------------------------
static void AddItem(TaskItem it) {
    g_app.cur().push_back(it);
    ListViewRefresh(g_list);
    ListViewSetSel(g_list, (int)g_app.cur().size() - 1);
    SyncItemRow();
    SaveConfig();
}

static void DoCapture() {
    if (g_picking) return;
    std::wstring path;
    if (!PickRegionSavePng(g_hMain, path)) {
        SetStatusText(L"截图已取消");
        RestoreMainWindow();
        return;
    }
    TaskItem it;
    it.kind = ItemKind::Image;
    it.image = path;
    it.similarity = 90;
    AddItem(it);
    SetStatusText(Format(L"已添加模板图 %s —— 运行时会在屏幕上找它并点击命中处",
                         FileNameOf(path).c_str()));
    RestoreMainWindow();
}

static void DoBrowse() {
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0所有文件\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"选择要找的图片";
    if (!GetOpenFileNameW(&ofn)) return;

    EnsureDir(DataDir());
    EnsureDir(ImagesDir());
    const std::wstring src(file);
    std::wstring dst = ImagesDir() + L"\\img_" + NowStamp() + L"_" + FileNameOf(src);
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        SetStatusText(L"复制图片失败，请换一张试试");
        return;
    }
    TaskItem it;
    it.kind = ItemKind::Image;
    it.image = dst;
    it.similarity = 90;
    AddItem(it);
    SetStatusText(Format(L"已添加模板图 %s", FileNameOf(dst).c_str()));
    RestoreMainWindow();
}

static void DoPickPoint() {
    if (g_picking) return;
    POINT pt = {};
    if (!PickPoint(g_hMain, pt)) { SetStatusText(L"坐标拾取已取消"); RestoreMainWindow(); return; }
    TaskItem it;
    it.kind = ItemKind::Point;
    it.x = pt.x;
    it.y = pt.y;
    AddItem(it);
    SetStatusText(Format(L"已添加坐标 (%d, %d)", pt.x, pt.y));
    RestoreMainWindow();
}

static void DoRememberCursor() {
    if (EngineRunning() || g_picking) return;
    POINT pt = {};
    GetCursorPos(&pt);
    TaskItem it;
    it.kind = ItemKind::Point;
    it.x = pt.x;
    it.y = pt.y;
    AddItem(it);
    SetStatusText(Format(L"已记住鼠标当前坐标 (%d, %d)", pt.x, pt.y));
}

// ---------------------------------------------------------------------------
// 列表操作
// ---------------------------------------------------------------------------
static void MoveSel(int delta) {
    const int sel = ListViewSel(g_list);
    std::vector<TaskItem>& v = g_app.cur();
    const int n = (int)v.size();
    if (sel < 0 || sel >= n) return;
    const int dst = sel + delta;
    if (dst < 0 || dst >= n) return;
    std::swap(v[(size_t)sel], v[(size_t)dst]);
    ListViewSetSel(g_list, dst);
    ListViewRefresh(g_list);
    SyncItemRow();
    SaveConfig();
}

static void DeleteSel() {
    const int sel = ListViewSel(g_list);
    std::vector<TaskItem>& v = g_app.cur();
    if (sel < 0 || sel >= (int)v.size()) return;
    v.erase(v.begin() + sel);
    ListViewSetSel(g_list, -1);
    ListViewRefresh(g_list);
    SyncItemRow();
    SaveConfig();
}

static void ClearAll() {
    if (g_app.cur().empty()) return;
    g_app.cur().clear();
    ListViewSetSel(g_list, -1);
    ListViewRefresh(g_list);
    SyncItemRow();
    SaveConfig();
    SetStatusText(L"当前模式的列表已清空");
}

// ---------------------------------------------------------------------------
// 主窗口过程
// ---------------------------------------------------------------------------
static void PaintMain(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush bg(Theme::Bg);
        g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
        Gdiplus::Pen pen(Theme::Border, 1.0f);
        g.DrawLine(&pen, (Gdiplus::REAL)S(18), (Gdiplus::REAL)S(442), (Gdiplus::REAL)(rc.right - S(18)), (Gdiplus::REAL)S(442));
        g.DrawLine(&pen, (Gdiplus::REAL)S(18), (Gdiplus::REAL)S(514), (Gdiplus::REAL)(rc.right - S(18)), (Gdiplus::REAL)S(514));
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void DrawStatusItem(DRAWITEMSTRUCT* dis) {
    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    RECT rc = dis->rcItem;
    Gdiplus::SolidBrush bg(Theme::Bg);
    g.FillRectangle(&bg, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);

    std::wstring text;
    {
        std::lock_guard<std::mutex> lk(g_status.mtx);
        text = g_status.text;
    }
    if (text.empty()) text = L"就绪";
    const bool running = EngineRunning();

    Gdiplus::Color c = Theme::TextDim;
    if (running) c = Theme::Accent;
    else if (text.find(L"失败") != std::wstring::npos ||
             text.find(L"没有") != std::wstring::npos) c = Gdiplus::Color(255, 226, 160, 96);

    // 运行中画一个呼吸点，一眼能看出程序在干活
    int tx = rc.left;
    if (running) {
        Gdiplus::SolidBrush dot(Theme::Success);
        g.FillEllipse(&dot, (Gdiplus::REAL)rc.left + S(2), (Gdiplus::REAL)rc.top + S(10),
                      (Gdiplus::REAL)S(8), (Gdiplus::REAL)S(8));
        tx += S(16);
    }
    UiDrawText(g, text,
               Gdiplus::RectF((Gdiplus::REAL)tx, (Gdiplus::REAL)rc.top,
                              (Gdiplus::REAL)(rc.right - tx), (Gdiplus::REAL)(rc.bottom - rc.top)),
               UiGpFont(13, false), c, 0, true, true);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: PaintMain(hwnd); return 0;

        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            HWND ctl = (HWND)lp;
            if (ctl == ui.editX || ctl == ui.editY) {          // 被禁用的 EDIT 走 STATIC 分支
                SetTextColor(dc, Theme::Ref(Theme::TextDim));
                SetBkColor(dc, Theme::Ref(Theme::EditBg));
                return (LRESULT)g_brEdit;
            }
            SetTextColor(dc, ctl == ui.lblItemHint ? Theme::Ref(Theme::TextDim) : Theme::Ref(Theme::Text));
            SetBkColor(dc, Theme::Ref(Theme::Bg));
            return (LRESULT)g_brBg;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, Theme::Ref(Theme::Text));
            SetBkColor(dc, Theme::Ref(Theme::EditBg));
            return (LRESULT)g_brEdit;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
            if (dis->hwndItem == ui.status) { DrawStatusItem(dis); return TRUE; }
            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == IDC_LIST) {
                if (code == LVN_CHANGED) { SyncItemRow(); SaveConfig(); }
                else if (code == LVN_SELCHANGED) { SyncItemRow(); }
                return 0;
            }
            switch (id) {
                case IDC_TAB_IMAGE: g_app.tab = 0; ListViewSetData(g_list, 0, &g_app.images); ApplyTab(); SaveConfig(); return 0;
                case IDC_TAB_POINT: g_app.tab = 1; ListViewSetData(g_list, 1, &g_app.points); ApplyTab(); SaveConfig(); return 0;

                case IDC_BTN_CAPTURE:
                    if (g_app.tab == 0) DoCapture(); else DoPickPoint();
                    return 0;
                case IDC_BTN_BROWSE:
                    if (g_app.tab == 0) DoBrowse(); else DoRememberCursor();
                    return 0;

                case IDC_BTN_UP:    MoveSel(-1); return 0;
                case IDC_BTN_DOWN:  MoveSel(1);  return 0;
                case IDC_BTN_DEL:   DeleteSel(); return 0;
                case IDC_BTN_CLEAR: ClearAll();  return 0;

                case IDC_BTN_CLICKMODE: {
                    const int sel = ListViewSel(g_list);
                    std::vector<TaskItem>& v = g_app.cur();
                    if (sel < 0 || sel >= (int)v.size()) return 0;
                    v[(size_t)sel].click = (ClickKind)(((int)v[(size_t)sel].click + 1) % 4);
                    SyncItemRow();
                    ListViewRefresh(g_list);
                    SaveConfig();
                    return 0;
                }
                case IDC_BTN_SIM_MINUS:
                case IDC_BTN_SIM_PLUS: {
                    const int sel = ListViewSel(g_list);
                    std::vector<TaskItem>& v = g_app.cur();
                    if (sel < 0 || sel >= (int)v.size()) return 0;
                    int s = v[(size_t)sel].similarity + (id == IDC_BTN_SIM_PLUS ? 5 : -5);
                    if (s < 50) s = 50;
                    if (s > 100) s = 100;
                    v[(size_t)sel].similarity = s;
                    SyncItemRow();
                    ListViewRefresh(g_list);
                    SaveConfig();
                    return 0;
                }

                case IDC_SEG_SKIP:  g_app.run.onNotFound = NotFoundAction::Skip;  SyncRunRow(); SaveConfig(); return 0;
                case IDC_SEG_RETRY: g_app.run.onNotFound = NotFoundAction::Retry; SyncRunRow(); SaveConfig(); return 0;
                case IDC_SEG_STOP:  g_app.run.onNotFound = NotFoundAction::Stop;  SyncRunRow(); SaveConfig(); return 0;
                case IDC_SEG_SCOPE_CUR:  g_app.scopeBoth = 0; SyncRunRow(); SaveConfig(); return 0;
                case IDC_SEG_SCOPE_BOTH: g_app.scopeBoth = 1; SyncRunRow(); SaveConfig(); return 0;

                case IDC_BTN_START: StartRun(); return 0;
                case IDC_BTN_STOP:  StopRun();  return 0;

                case IDC_EDIT_STEPDELAY:
                case IDC_EDIT_LOOP:
                case IDC_EDIT_STARTDELAY:
                case IDC_EDIT_RETRY:
                case IDC_EDIT_RETRYIV:
                    if (code == EN_KILLFOCUS) { PullRunRow(); SyncRunRow(); SaveConfig(); }
                    return 0;

                case IDC_EDIT_X:
                case IDC_EDIT_Y: {
                    if (code != EN_KILLFOCUS) return 0;
                    const int s2 = ListViewSel(g_list);
                    std::vector<TaskItem>& v2 = g_app.cur();
                    if (s2 < 0 || s2 >= (int)v2.size()) return 0;
                    v2[(size_t)s2].x = GetEditInt(ui.editX, v2[(size_t)s2].x, -32768, 32767);
                    v2[(size_t)s2].y = GetEditInt(ui.editY, v2[(size_t)s2].y, -32768, 32767);
                    SyncItemRow();
                    ListViewRefresh(g_list);
                    SaveConfig();
                    return 0;
                }
            }
            return 0;
        }

        case WM_HOTKEY: {
            if (g_picking) return 0;
            if (wp == 1) { if (EngineRunning()) StopRun(); else StartRun(); }
            else if (wp == 2) { StopRun(); }
            else if (wp == 3) { DoRememberCursor(); }
            return 0;
        }

        case WM_APP_ENGINE_DONE:
            EngineJoinIfNeeded();
            RestoreMainWindow();
            BtnSetDisabled(ui.btnStart, false);
            BtnSetDisabled(ui.btnStop, true);
            InvalidateRect(ui.status, nullptr, FALSE);
            return 0;

        case WM_TIMER: {
            if (wp != 1) return 0;
            {
                std::wstring cur;
                {
                    std::lock_guard<std::mutex> lk(g_status.mtx);
                    cur = g_status.text;
                }
                static std::wstring last;
                if (cur != last) {
                    last = cur;
                    Log(L"UI 状态更新: %s", cur.c_str());
                    SetWindowTextW(ui.status, cur.c_str());
                    InvalidateRect(ui.status, nullptr, FALSE);
                }
            }
            const bool run = EngineRunning();
            static bool lastRun = false;
            if (run != lastRun) {
                lastRun = run;
                BtnSetDisabled(ui.btnStart, run);
                BtnSetDisabled(ui.btnStop, !run);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            const int nd = HIWORD(wp);
            UiOnDpiChanged(nd);
            RECT* nr = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, nr->left, nr->top, nr->right - nr->left,
                         nr->bottom - nr->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }

        case WM_CLOSE:
            if (EngineRunning()) { EngineStop(); EngineJoinIfNeeded(); }
            PullRunRow();
            SaveConfig();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
static void EnableDpiAwareness() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    typedef BOOL(WINAPI* PFN)(HANDLE);
    PFN p = (PFN)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (p) { p((HANDLE)-4); return; }          // PER_MONITOR_AWARE_V2
    typedef BOOL(WINAPI* PFN2)(void);
    PFN2 p2 = (PFN2)GetProcAddress(u, "SetProcessDPIAware");
    if (p2) p2();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    EnableDpiAwareness();                       // 必须在任何取尺寸之前

    // 单实例：第二次启动就把已有窗口叫到前面来
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"AutoClick_SingleInstance_v1");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"AutoClickMain", nullptr);
        if (prev) { ShowWindow(prev, SW_RESTORE); SetForegroundWindow(prev); }
        CloseHandle(mutex);
        return 0;
    }

    g_hInst = hInst;
    timeBeginPeriod(1);
    InstallCrashLogger();
    LogInit();
    Log(L"---- 启动，日志开启 ----");

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gpToken = 0;
    Gdiplus::GdiplusStartup(&gpToken, &gsi, nullptr);

    UiInit(hInst);
    LoadConfig();

    g_brBg   = CreateSolidBrush(Theme::Ref(Theme::Bg));
    g_brEdit = CreateSolidBrush(Theme::Ref(Theme::EditBg));

    RegisterBtnClass();
    ListViewRegister(hInst);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AutoClickMain";

    if (!RegisterClassExW(&wc)) return 1;

    RECT rc = { 0, 0, S(CW), S(CH) };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int wx = (sw - ww) / 2, wy = (sh - wh) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;

    g_hMain = CreateWindowExW(0, L"AutoClickMain", L"自动点击助手",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    if (!g_hMain) return 1;

    UiApplyDarkTitleBar(g_hMain);

    // 控件
    ui.tabImage = MakeBtn(g_hMain, IDC_TAB_IMAGE, L"找图模式", BS_Tab);
    ui.tabPoint = MakeBtn(g_hMain, IDC_TAB_POINT, L"坐标模式", BS_Tab);
    ui.btnA = MakeBtn(g_hMain, IDC_BTN_CAPTURE, L"截图添加", BS_Normal);
    ui.btnB = MakeBtn(g_hMain, IDC_BTN_BROWSE,  L"从文件添加", BS_Normal);
    ui.btnUp    = MakeBtn(g_hMain, IDC_BTN_UP,    L"↑", BS_Normal);
    ui.btnDown  = MakeBtn(g_hMain, IDC_BTN_DOWN,  L"↓", BS_Normal);
    ui.btnDel   = MakeBtn(g_hMain, IDC_BTN_DEL,   L"删除", BS_Danger);
    ui.btnClear = MakeBtn(g_hMain, IDC_BTN_CLEAR, L"清空", BS_Danger);

    ui.btnClickMode = MakeBtn(g_hMain, IDC_BTN_CLICKMODE, L"点击方式：左键单击", BS_Normal);
    ui.btnSimMinus  = MakeBtn(g_hMain, IDC_BTN_SIM_MINUS, L"−", BS_Normal);
    ui.btnSimPlus   = MakeBtn(g_hMain, IDC_BTN_SIM_PLUS,  L"＋", BS_Normal);
    ui.lblSimVal = CreateWindowExW(0, L"STATIC", L"90%", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                   0, 0, 10, 10, g_hMain, nullptr, hInst, nullptr);

    ui.segSkip  = MakeBtn(g_hMain, IDC_SEG_SKIP,  L"立即跳过", BS_Seg);
    ui.segRetry = MakeBtn(g_hMain, IDC_SEG_RETRY, L"重试等待", BS_Seg);
    ui.segStop  = MakeBtn(g_hMain, IDC_SEG_STOP,  L"停止运行", BS_Seg);
    ui.segScopeCur  = MakeBtn(g_hMain, IDC_SEG_SCOPE_CUR,  L"仅当前模式", BS_Seg);
    ui.segScopeBoth = MakeBtn(g_hMain, IDC_SEG_SCOPE_BOTH, L"两个列表合并", BS_Seg);

    ui.btnStart = MakeBtn(g_hMain, IDC_BTN_START, L"▶  开始执行  (F6)", BS_Primary);
    ui.btnStop  = MakeBtn(g_hMain, IDC_BTN_STOP,  L"■  停止", BS_Danger);
    BtnSetDisabled(ui.btnStop, true);

    ui.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                0, 0, 10, 10, g_hMain, nullptr, hInst, nullptr);

    g_list = ListViewCreate(g_hMain, IDC_LIST, g_app.tab,
                            g_app.tab == 0 ? &g_app.images : &g_app.points);
    ui.list = ListViewHwnd(g_list);

    g_statusShown.clear();
    Layout();              // 先把控件都建出来，后面填值才有效
    SyncRunRow();
    ApplyTab();
    SyncItemRow();

    RegisterHotKey(g_hMain, 1, MOD_NOREPEAT, VK_F6);
    RegisterHotKey(g_hMain, 2, MOD_NOREPEAT, VK_F12);
    RegisterHotKey(g_hMain, 3, MOD_NOREPEAT, VK_F7);
    SetTimer(g_hMain, 1, 120, nullptr);

    ShowWindow(g_hMain, SW_SHOW);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EngineStop();
    EngineJoinIfNeeded();
    if (mutex) CloseHandle(mutex);
    if (g_brBg) DeleteObject(g_brBg);
    if (g_brEdit) DeleteObject(g_brEdit);
    Gdiplus::GdiplusShutdown(gpToken);
    timeEndPeriod(1);
    return (int)msg.wParam;
}
