// ===========================================================================
//  app.h  --  自动点击助手：全局共享的类型、常量与接口声明
//  纯 Win32 API + GDI+，无第三方框架。
// ===========================================================================
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
// WIN32_LEAN_AND_MEAN 会把 OLE 相关头文件排除掉，而 GDI+ 的头文件需要它们
// （PROPID、IStream、byte 都在里面），所以必须显式补上，且顺序不能变。
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

// ---------------------------------------------------------------------------
// 控件 ID
// ---------------------------------------------------------------------------
enum : int {
    IDC_TAB_IMAGE       = 1001,   // 顶部标签：找图模式
    IDC_TAB_POINT       = 1002,   // 顶部标签：坐标模式

    IDC_BTN_CAPTURE     = 1010,   // 截图添加
    IDC_BTN_BROWSE      = 1011,   // 从文件添加
    IDC_BTN_PICK        = 1012,   // 拾取坐标
    IDC_BTN_MANUAL      = 1013,   // 手动输入坐标
    IDC_BTN_UP          = 1014,   // 上移
    IDC_BTN_DOWN        = 1015,   // 下移
    IDC_BTN_DEL         = 1016,   // 删除选中
    IDC_BTN_CLEAR       = 1017,   // 清空列表

    IDC_LIST            = 1020,   // 自绘列表（当前标签页的那一个）

    IDC_BTN_CLICKMODE   = 1030,   // 选中项：点击方式（循环切换）
    IDC_BTN_SIM_MINUS   = 1031,   // 选中项：相似度 -
    IDC_BTN_SIM_PLUS    = 1032,   // 选中项：相似度 +

    IDC_EDIT_STEPDELAY  = 1040,   // 每步间隔
    IDC_EDIT_LOOP       = 1041,   // 循环次数
    IDC_EDIT_STARTDELAY = 1042,   // 开始前延时
    IDC_EDIT_RETRY      = 1043,   // 重试次数
    IDC_EDIT_RETRYIV    = 1044,   // 重试间隔
    IDC_EDIT_X          = 1045,   // 选中坐标项的 X
    IDC_EDIT_Y          = 1046,   // 选中坐标项的 Y
    IDC_SEG_SKIP        = 1050,   // 找不到时：跳过
    IDC_SEG_RETRY       = 1051,   // 找不到时：重试
    IDC_SEG_STOP        = 1052,   // 找不到时：停止
    IDC_SEG_SCOPE_CUR   = 1053,   // 执行范围：仅当前模式
    IDC_SEG_SCOPE_BOTH  = 1054,   // 执行范围：两个列表合并

    IDC_BTN_START       = 1060,   // 开始执行
    IDC_BTN_STOP        = 1061,   // 停止

    IDC_STATUS          = 1070,   // 状态文字（自绘 STATIC）
};

// 自定义消息
#define WM_APP_ENGINE_DONE  (WM_APP + 1)   // 执行线程结束

// 列表控件向上层发的自定义通知（放在 WM_COMMAND 的 HIWORD(wParam) 里）
#define LVN_SELCHANGED      1   // 只是选中项变了，不需要存盘
#define LVN_CHANGED         2   // 列表内容变了（排序/删除/勾选），要立刻存盘

// ---------------------------------------------------------------------------
// 数据模型
// ---------------------------------------------------------------------------
enum class ItemKind { Image, Point };

enum class ClickKind {
    LeftSingle = 0,
    LeftDouble = 1,
    RightSingle = 2,
    MiddleSingle = 3,
};

enum class NotFoundAction { Skip = 0, Retry = 1, Stop = 2 };

struct TaskItem {
    ItemKind     kind      = ItemKind::Point;
    std::wstring image;                 // 找图项：模板图绝对路径
    int          x         = 0;         // 坐标项：屏幕 X
    int          y         = 0;         // 坐标项：屏幕 Y
    int          similarity = 90;       // 找图项：相似度阈值（百分比）
    ClickKind    click     = ClickKind::LeftSingle;
    bool         enabled   = true;
};

struct RunSettings {
    int            stepDelayMs    = 500;    // 每一步之间的间隔
    int            loopCount      = 1;      // 0 = 无限循环
    int            startDelayMs   = 1000;   // 按下开始后等待多久才动手（给用户切窗口的时间）
    NotFoundAction onNotFound     = NotFoundAction::Skip;
    int            retryCount     = 10;     // 找不到时重试次数
    int            retryIntervalMs = 300;   // 每次重试之间的等待
};

struct AppState {
    std::vector<TaskItem> images;   // 找图模式的列表
    std::vector<TaskItem> points;   // 坐标模式的列表
    RunSettings           run;
    int                   tab = 0;  // 0 = 找图，1 = 坐标
    int                   scopeBoth = 0;  // 0 = 只跑当前模式，1 = 两个列表按顺序合并

    std::vector<TaskItem>& cur() { return tab == 0 ? images : points; }
};

extern AppState g_app;              // 主线程读写；执行线程拿到的是值拷贝

// ---------------------------------------------------------------------------
// 全局 DPI 与主题（ui_common.cpp）
// ---------------------------------------------------------------------------
extern int g_dpi;
inline int S(int v) { return MulDiv(v, g_dpi, 96); }   // 逻辑像素 -> 物理像素

namespace Theme {
    extern Gdiplus::Color Bg;          // 窗口背景
    extern Gdiplus::Color Panel;       // 面板 / 工具条
    extern Gdiplus::Color ListBg;      // 列表背景
    extern Gdiplus::Color RowAlt;      // 隔行
    extern Gdiplus::Color RowHover;
    extern Gdiplus::Color RowSel;
    extern Gdiplus::Color EditBg;
    extern Gdiplus::Color EditText;
    extern Gdiplus::Color Border;
    extern Gdiplus::Color Text;
    extern Gdiplus::Color TextDim;
    extern Gdiplus::Color Accent;      // 主色（蓝）
    extern Gdiplus::Color AccentDim;
    extern Gdiplus::Color Danger;
    extern Gdiplus::Color Success;
    extern Gdiplus::Color Track;       // 滚动条槽
    extern Gdiplus::Color Thumb;       // 滚动条滑块

    COLORREF Ref(const Gdiplus::Color& c);   // 转 COLORREF（给 GDI 用）
}

void  UiInit(HINSTANCE hInst);          // 取初始 DPI、建主题画刷
void  UiOnDpiChanged(int newDpi);       // 更新 g_dpi
HFONT UiFont(int px, bool bold = false);          // 按 DPI 缩放的字号取字体（给子控件用）
HFONT UiFontRaw(int physPx, bool bold = false);   // 指定物理像素
Gdiplus::Font* UiGpFont(int px, bool bold = false);   // 给 GDI+ 自绘用
void  UiDrawRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                      const Gdiplus::Color& fill, const Gdiplus::Color& border,
                      float borderWidth = 1.0f);
void  UiDrawText(Gdiplus::Graphics& g, const std::wstring& s, const Gdiplus::RectF& r,
                 Gdiplus::Font* font, const Gdiplus::Color& color, int align /*0左1中2右*/,
                 bool vcenter = true, bool ellipsis = true);

// 深色标题栏
void  UiApplyDarkTitleBar(HWND hwnd);

// ---------------------------------------------------------------------------
// 日志（只在设置了环境变量 AUTO_CLICK_LOG=1 时写文件，正常使用不产生任何文件）
// 出错日志（崩溃）永远记录，方便定位问题
// ---------------------------------------------------------------------------
void LogInit();
void Log(const wchar_t* fmt, ...);
void InstallCrashLogger();      // 崩溃时把异常码与调用栈写进日志

// ---------------------------------------------------------------------------
// 路径 / 配置（ui_common.cpp）
// ---------------------------------------------------------------------------
std::wstring ExeDir();
std::wstring DataDir();                 // 支持环境变量 AUTO_CLICK_DATA_DIR 重定向（测试用）
std::wstring ImagesDir();               // DataDir()\images
bool         EnsureDir(const std::wstring& dir);
std::wstring NowStamp();                // 20260101_120000
std::wstring FileNameOf(const std::wstring& path);
std::wstring Format(const wchar_t* fmt, ...);

bool LoadConfig();
bool SaveConfig();

std::wstring ClickKindName(ClickKind k);

// ---------------------------------------------------------------------------
// 屏幕抓取与模板匹配（imaging.cpp）
// ---------------------------------------------------------------------------
struct ScreenShot {
    int w = 0, h = 0;              // 虚拟桌面尺寸（物理像素）
    int left = 0, top = 0;         // 虚拟桌面左上角（多屏时可能为负）
    std::vector<uint32_t> px;      // BGRA，行优先，top-down
    bool valid() const { return w > 0 && h > 0 && !px.empty(); }
};

// 抓取整个虚拟桌面。返回 false 表示抓取失败。
bool CaptureScreen(ScreenShot& out);

// 把 ScreenShot 转成 GDI+ Bitmap（调用方负责 delete）
Gdiplus::Bitmap* ScreenToBitmap(const ScreenShot& sc);

struct TemplateImage {
    std::wstring          path;
    int                   w = 0, h = 0;
    std::vector<uint8_t>  gray;     // 灰度模板，行优先
    double                mean = 0.0;
    double                varSum = 0.0;   // sum((t-mean)^2)
    bool                  ok = false;
};

std::shared_ptr<TemplateImage> LoadTemplate(const std::wstring& path);
std::shared_ptr<TemplateImage> GetTemplate(const std::wstring& path);   // 带缓存

struct MatchResult {
    bool   found = false;
    int    x = 0, y = 0;        // 命中区域左上角（屏幕物理坐标）
    int    cx = 0, cy = 0;      // 命中区域中心（屏幕物理坐标）
    double score = 0.0;         // 0..1
    double ms = 0.0;            // 耗时
    int    scanned = 0;         // 参与精算的候选数
};

MatchResult FindTemplate(const ScreenShot& sc, const TemplateImage& tpl, int minScorePct);

// 缩略图（给列表用），按路径缓存
Gdiplus::Bitmap* GetThumb(const std::wstring& path, int maxW, int maxH);

// 把 BGRA 像素存成 PNG
bool SavePngRgba(const uint32_t* px, int w, int h, const std::wstring& path);

// ---------------------------------------------------------------------------
// 覆盖层（overlay.cpp）
// ---------------------------------------------------------------------------
extern bool g_picking;      // 覆盖层开着时，主窗口忽略热键与“开始”

// 框选截图：成功返回 true，并把裁剪结果保存为 PNG；outPath 返回路径
bool PickRegionSavePng(HWND owner, std::wstring& outPath);
// 坐标拾取：成功返回 true，out 为屏幕物理坐标
bool PickPoint(HWND owner, POINT& out);

// ---------------------------------------------------------------------------
// 自绘列表控件（listview.cpp）
// ---------------------------------------------------------------------------
struct ListView;
void     ListViewRegister(HINSTANCE hInst);
ListView* ListViewCreate(HWND parent, int id, int mode, std::vector<TaskItem>* items);
HWND     ListViewHwnd(ListView* lv);
void     ListViewRefresh(ListView* lv);
int      ListViewSel(ListView* lv);
void     ListViewSetSel(ListView* lv, int idx);
// 切换标签页时换掉背后的数据源
void     ListViewSetData(ListView* lv, int mode, std::vector<TaskItem>* items);

// ---------------------------------------------------------------------------
// 执行引擎（engine.cpp）
// ---------------------------------------------------------------------------
struct EngineStatus {
    std::mutex   mtx;
    std::wstring text;      // 状态栏主文字
    int          step = 0;
    int          total = 0;
    bool         running = false;
};
extern EngineStatus g_status;

bool EngineStart(HWND notify, const std::vector<TaskItem>& items, const RunSettings& rs);
void EngineStop();
bool EngineRunning();
void EngineJoinIfNeeded();
