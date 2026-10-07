// ===========================================================================
//  ui_common.cpp  --  DPI 适配、深色主题、字体缓存、绘图辅助、路径与配置读写
// ===========================================================================
#include "app.h"

#include <dwmapi.h>
#include <map>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>

#pragma comment(lib, "dwmapi.lib")

int g_dpi = 96;

// ---------------------------------------------------------------------------
// 主题配色（深色）
// ---------------------------------------------------------------------------
namespace Theme {
    Gdiplus::Color Bg          (255,  30,  30,  34);
    Gdiplus::Color Panel       (255,  40,  40,  46);
    Gdiplus::Color ListBg      (255,  25,  25,  29);
    Gdiplus::Color RowAlt      (255,  31,  31,  36);
    Gdiplus::Color RowHover    (255,  43,  43,  50);
    Gdiplus::Color RowSel      (255,  38,  68, 108);
    Gdiplus::Color EditBg      (255,  56,  56,  66);
    Gdiplus::Color EditText    (255, 235, 235, 240);
    Gdiplus::Color Border      (255,  62,  62,  72);
    Gdiplus::Color Text        (255, 228, 228, 233);
    Gdiplus::Color TextDim     (255, 146, 146, 156);
    Gdiplus::Color Accent      (255,  58, 130, 247);
    Gdiplus::Color AccentDim   (255,  40,  86, 160);
    Gdiplus::Color Danger      (255, 214,  82,  82);
    Gdiplus::Color Success     (255,  76, 190, 122);
    Gdiplus::Color Track       (255,  36,  36,  41);
    Gdiplus::Color Thumb       (255,  78,  78,  90);

    COLORREF Ref(const Gdiplus::Color& c) {
        return RGB(c.GetR(), c.GetG(), c.GetB());
    }
}

// ---------------------------------------------------------------------------
// 字体缓存
// ---------------------------------------------------------------------------
static std::map<std::pair<int, bool>, HFONT>       g_hfonts;
static std::map<std::pair<int, bool>, Gdiplus::Font*> g_gpfonts;
static const wchar_t* g_family = L"Microsoft YaHei UI";

static const wchar_t* PickFamily() {
    static int checked = 0;
    static const wchar_t* chosen = L"Microsoft YaHei UI";
    if (checked) return chosen;
    checked = 1;
    const wchar_t* cands[] = { L"Microsoft YaHei UI", L"Microsoft YaHei", L"Microsoft Sans Serif" };
    for (const wchar_t* c : cands) {
        Gdiplus::FontFamily fam(c);
        if (fam.IsAvailable()) { chosen = c; break; }
    }
    return chosen;
}

static HFONT MakeFont(int physPx, bool bold) {
    int height = -physPx;                     // 负值 = 字符高度（不含内部行距）
    return CreateFontW(height, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, PickFamily());
}

HFONT UiFontRaw(int physPx, bool bold) {
    auto key = std::make_pair(physPx, bold);
    auto it = g_hfonts.find(key);
    if (it != g_hfonts.end()) return it->second;
    HFONT f = MakeFont(physPx, bold);
    g_hfonts[key] = f;
    return f;
}

HFONT UiFont(int px, bool bold) { return UiFontRaw(S(px), bold); }

static Gdiplus::Font* UiGpFontRaw(int physPx, bool bold) {
    auto key = std::make_pair(physPx, bold);
    auto it = g_gpfonts.find(key);
    if (it != g_gpfonts.end()) return it->second;
    Gdiplus::Font* f = new Gdiplus::Font(PickFamily(), (Gdiplus::REAL)physPx,
                                         bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                                         Gdiplus::UnitPixel);
    if (f->GetLastStatus() != Gdiplus::Ok) {
        delete f;
        f = new Gdiplus::Font(L"Microsoft Sans Serif", (Gdiplus::REAL)physPx,
                              bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                              Gdiplus::UnitPixel);
    }
    g_gpfonts[key] = f;
    return f;
}

Gdiplus::Font* UiGpFont(int px, bool bold) { return UiGpFontRaw(S(px), bold); }

// ---------------------------------------------------------------------------
// 初始化 / DPI
// ---------------------------------------------------------------------------
void UiInit(HINSTANCE) {
    HDC dc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    if (g_dpi <= 0) g_dpi = 96;
}

void UiOnDpiChanged(int newDpi) {
    if (newDpi <= 0 || newDpi == g_dpi) return;
    g_dpi = newDpi;
    for (auto& kv : g_hfonts) { DeleteObject(kv.second); }
    g_hfonts.clear();          // 字体按新 DPI 重建；GDI+ 字体按需重建，旧的留给进程退出回收
}

// ---------------------------------------------------------------------------
// 绘图辅助
// ---------------------------------------------------------------------------
static void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float rad) {
    float maxRad = (r.Width < r.Height ? r.Width : r.Height) / 2.0f;
    if (rad > maxRad) rad = maxRad;
    if (rad < 0.5f) { path.AddRectangle(r); return; }
    const float d = rad * 2.0f;
    path.AddArc(r.X,             r.Y,              d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y,            d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d,   0.0f, 90.0f);
    path.AddArc(r.X,             r.Y + r.Height - d, d, d,  90.0f, 90.0f);
    path.CloseFigure();
}

void UiDrawRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                     const Gdiplus::Color& fill, const Gdiplus::Color& border,
                     float borderWidth) {
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, r, radius);
    if (fill.GetA() > 0) {
        Gdiplus::SolidBrush b(fill);
        g.FillPath(&b, &path);
    }
    if (border.GetA() > 0 && borderWidth > 0.0f) {
        Gdiplus::Pen p(border, borderWidth);
        g.DrawPath(&p, &path);
    }
}

void UiDrawText(Gdiplus::Graphics& g, const std::wstring& s, const Gdiplus::RectF& r,
                Gdiplus::Font* f, const Gdiplus::Color& color, int align, bool vcenter, bool ellipsis) {
    if (s.empty() || !f) return;
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat fmt;
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetTrimming(ellipsis ? Gdiplus::StringTrimmingEllipsisCharacter : Gdiplus::StringTrimmingNone);
    fmt.SetAlignment(align == 0 ? Gdiplus::StringAlignmentNear
                    : align == 2 ? Gdiplus::StringAlignmentFar
                                 : Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(vcenter ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    g.DrawString(s.c_str(), (INT)s.size(), f, r, &fmt, &brush);
}

void UiApplyDarkTitleBar(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI* PFN)(HWND, DWORD, LPCVOID, DWORD);
    PFN set = (PFN)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set) {
        BOOL dark = TRUE;
        if (FAILED(set(hwnd, 20, &dark, sizeof(dark))))   // DWMWA_USE_IMMERSIVE_DARK_MODE
            set(hwnd, 19, &dark, sizeof(dark));           // 旧版 Win10 的编号
    }
    FreeLibrary(dwm);
}

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------
static std::mutex g_logMtx;
static int  g_logMode = -1;      // -1 未探测，0 关，1 开

void LogInit() {
    if (g_logMode >= 0) return;
    wchar_t v[16] = {};
    DWORD n = GetEnvironmentVariableW(L"AUTO_CLICK_LOG", v, 16);
    g_logMode = (n > 0 && v[0] == L'1') ? 1 : 0;
}

static void LogRaw(const wchar_t* line) {
    std::lock_guard<std::mutex> lk(g_logMtx);
    std::wstring path = DataDir() + L"\\debug.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") != 0 || !f) return;
    fputws(line, f);
    fputws(L"\n", f);
    fclose(f);
}

void Log(const wchar_t* fmt, ...) {
    LogInit();
    if (g_logMode != 1) return;
    wchar_t buf[1200];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    LogRaw(Format(L"[%02d:%02d:%02d.%03d] %s", st.wHour, st.wMinute, st.wSecond,
                  st.wMilliseconds, buf).c_str());
}

extern "C" __declspec(dllimport) unsigned short __stdcall
RtlCaptureStackBackTrace(unsigned long Skip, unsigned long Count, void** Trace, unsigned long* Hash);

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    LogRaw(L"==================== 程序异常 ====================");
    if (ep && ep->ExceptionRecord) {
        LogRaw(Format(L"异常代码 = 0x%08X   出错地址 = %p",
                      ep->ExceptionRecord->ExceptionCode,
                      ep->ExceptionRecord->ExceptionAddress).c_str());
    }
    void* frames[40] = {};
    unsigned short n = RtlCaptureStackBackTrace(0, 40, frames, nullptr);
    for (unsigned short i = 0; i < n; ++i)
        LogRaw(Format(L"  栈 #%02d  %p", i, frames[i]).c_str());
    LogRaw(L"=================================================");
    return EXCEPTION_EXECUTE_HANDLER;
}

static void TerminateFilter() {
    LogRaw(L"==================== std::terminate ====================");
    try {
        std::exception_ptr e = std::current_exception();
        if (e) std::rethrow_exception(e);
        LogRaw(L"（没有异常对象）");
    } catch (const std::exception& ex) {
        LogRaw(Format(L"异常信息: %hs", ex.what()).c_str());
    } catch (...) {
        LogRaw(L"未知类型的异常");
    }
    LogRaw(L"========================================================");
    abort();
}

void InstallCrashLogger() {
    SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate(TerminateFilter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG);   // 别再弹“调试错误”对话框
}

// ---------------------------------------------------------------------------
// 路径工具
// ---------------------------------------------------------------------------
std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring p(buf);
    size_t k = p.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? p : p.substr(0, k);
}

// 测试隔离：设置 AUTO_CLICK_DATA_DIR 可把配置与图片重定向到别处
std::wstring DataDir() {
    wchar_t ov[MAX_PATH * 2] = {};
    DWORD n = GetEnvironmentVariableW(L"AUTO_CLICK_DATA_DIR", ov, MAX_PATH * 2);
    if (n > 0 && n < MAX_PATH * 2) return std::wstring(ov);
    return ExeDir();
}

std::wstring ImagesDir() { return DataDir() + L"\\images"; }

bool EnsureDir(const std::wstring& dir) {
    DWORD a = GetFileAttributesW(dir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryW(dir.c_str(), nullptr) != 0;
}

std::wstring NowStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return Format(L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t k = path.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? path : path.substr(k + 1);
}

std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    return std::wstring(buf);
}

std::wstring ClickKindName(ClickKind k) {
    switch (k) {
        case ClickKind::LeftSingle:   return L"左键单击";
        case ClickKind::LeftDouble:   return L"左键双击";
        case ClickKind::RightSingle:  return L"右键单击";
        case ClickKind::MiddleSingle: return L"中键单击";
    }
    return L"左键单击";
}

// ---------------------------------------------------------------------------
// 配置文件：UTF-8（带 BOM）的简单键值文本，可读可手改
// ---------------------------------------------------------------------------
static std::wstring ConfigPath() { return DataDir() + L"\\config.ini"; }

static bool ReadAllTextUtf8(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size > 16u * 1024u * 1024u) { CloseHandle(h); return false; }
    std::string buf(size, '\0');
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf.empty() ? nullptr : &buf[0], size, &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    buf.resize(got);
    size_t off = 0;
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) off = 3;
    if (buf.size() <= off) { out.clear(); return true; }
    int need = MultiByteToWideChar(CP_UTF8, 0, buf.data() + off, (int)(buf.size() - off), nullptr, 0);
    if (need <= 0) return false;
    out.assign((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data() + off, (int)(buf.size() - off), &out[0], need);
    return true;
}

static bool WriteAllTextUtf8(const std::wstring& path, const std::wstring& text) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (need < 0) return false;
    std::string buf;
    buf.reserve((size_t)need + 3);
    buf.append("\xEF\xBB\xBF", 3);          // UTF-8 BOM，记事本才会正确认成 UTF-8
    std::string body((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &body[0], need, nullptr, nullptr);
    buf += body;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, buf.data(), (DWORD)buf.size(), &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == buf.size();
}

static std::wstring TrimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static int ToInt(const std::wstring& s, int def) {
    if (s.empty()) return def;
    wchar_t* end = nullptr;
    long v = wcstol(s.c_str(), &end, 10);
    if (end == s.c_str()) return def;
    return (int)v;
}

// 图片只存文件名（在 images 目录内时），保证整个文件夹可整体搬走
static std::wstring StorePath(const std::wstring& abs) {
    std::wstring dir = ImagesDir();
    if (abs.size() > dir.size() + 1) {
        if (_wcsnicmp(abs.c_str(), dir.c_str(), dir.size()) == 0 && abs[dir.size()] == L'\\')
            return abs.substr(dir.size() + 1);
    }
    return abs;
}

static std::wstring ResolvePath(const std::wstring& stored) {
    if (stored.size() > 1 && stored[1] == L':') return stored;              // 绝对路径
    if (stored.size() > 2 && stored[0] == L'\\' && stored[1] == L'\\') return stored;  // UNC
    return ImagesDir() + L"\\" + stored;
}

bool SaveConfig() {
    std::wstring t;
    t += L"# 自动点击助手 配置文件（可直接用记事本编辑）\n";
    t += L"# 图片路径为相对 images 目录的文件名\n\n";
    t += Format(L"tab=%d\n", g_app.tab);
    t += Format(L"scope=%d\n", g_app.scopeBoth);
    t += Format(L"stepDelay=%d\n", g_app.run.stepDelayMs);
    t += Format(L"loopCount=%d\n", g_app.run.loopCount);
    t += Format(L"startDelay=%d\n", g_app.run.startDelayMs);
    t += Format(L"onNotFound=%d\n", (int)g_app.run.onNotFound);
    t += Format(L"retryCount=%d\n", g_app.run.retryCount);
    t += Format(L"retryInterval=%d\n", g_app.run.retryIntervalMs);
    t += L"\n";

    auto dump = [&](const std::vector<TaskItem>& v) {
        for (const TaskItem& it : v) {
            if (it.kind == ItemKind::Image) {
                t += L"[image]\n";
                t += Format(L"path=%s\n", StorePath(it.image).c_str());
                t += Format(L"sim=%d\n", it.similarity);
            } else {
                t += L"[point]\n";
                t += Format(L"x=%d\n", it.x);
                t += Format(L"y=%d\n", it.y);
            }
            t += Format(L"click=%d\n", (int)it.click);
            t += Format(L"enabled=%d\n", it.enabled ? 1 : 0);
            t += L"\n";
        }
    };
    dump(g_app.images);
    dump(g_app.points);
    return WriteAllTextUtf8(ConfigPath(), t);
}

bool LoadConfig() {
    std::wstring text;
    if (!ReadAllTextUtf8(ConfigPath(), text)) return false;

    g_app.images.clear();
    g_app.points.clear();

    TaskItem cur;
    bool inItem = false;
    auto flush = [&]() {
        if (!inItem) return;
        if (cur.kind == ItemKind::Image) {
            if (!cur.image.empty()) g_app.images.push_back(cur);
        } else {
            g_app.points.push_back(cur);
        }
        cur = TaskItem();
        inItem = false;
    };

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        std::wstring line = (nl == std::wstring::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        pos = (nl == std::wstring::npos) ? text.size() + 1 : nl + 1;
        line = TrimW(line);
        if (line.empty() || line[0] == L'#') continue;

        if (line == L"[image]") { flush(); cur.kind = ItemKind::Image; inItem = true; continue; }
        if (line == L"[point]") { flush(); cur.kind = ItemKind::Point; inItem = true; continue; }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = TrimW(line.substr(0, eq));
        std::wstring val = TrimW(line.substr(eq + 1));

        if (inItem) {
            if (key == L"path")         cur.image = ResolvePath(val);
            else if (key == L"sim")     cur.similarity = ToInt(val, 90);
            else if (key == L"x")       cur.x = ToInt(val, 0);
            else if (key == L"y")       cur.y = ToInt(val, 0);
            else if (key == L"click")   cur.click = (ClickKind)ToInt(val, 0);
            else if (key == L"enabled") cur.enabled = ToInt(val, 1) != 0;
        } else {
            if (key == L"tab")               g_app.tab = ToInt(val, 0);
            else if (key == L"scope")        g_app.scopeBoth = ToInt(val, 0) ? 1 : 0;
            else if (key == L"stepDelay")    g_app.run.stepDelayMs = ToInt(val, 500);
            else if (key == L"loopCount")    g_app.run.loopCount = ToInt(val, 1);
            else if (key == L"startDelay")   g_app.run.startDelayMs = ToInt(val, 1000);
            else if (key == L"onNotFound")   g_app.run.onNotFound = (NotFoundAction)ToInt(val, 0);
            else if (key == L"retryCount")   g_app.run.retryCount = ToInt(val, 10);
            else if (key == L"retryInterval")g_app.run.retryIntervalMs = ToInt(val, 300);
        }
    }
    flush();

    // 钳制到合法范围，防止手改配置写坏
    if (g_app.tab != 0 && g_app.tab != 1) g_app.tab = 0;
    if (g_app.run.stepDelayMs  < 0)     g_app.run.stepDelayMs = 0;
    if (g_app.run.stepDelayMs  > 60000) g_app.run.stepDelayMs = 60000;
    if (g_app.run.startDelayMs < 0)     g_app.run.startDelayMs = 0;
    if (g_app.run.startDelayMs > 60000) g_app.run.startDelayMs = 60000;
    if (g_app.run.loopCount < 0)        g_app.run.loopCount = 0;
    if (g_app.run.retryCount < 0)       g_app.run.retryCount = 0;
    if (g_app.run.retryCount > 999)     g_app.run.retryCount = 999;
    if (g_app.run.retryIntervalMs < 0)  g_app.run.retryIntervalMs = 0;
    for (TaskItem& it : g_app.images) {
        if (it.similarity < 50)  it.similarity = 50;
        if (it.similarity > 100) it.similarity = 100;
    }
    return true;
}
