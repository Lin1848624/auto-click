// ===========================================================================
//  engine.cpp  --  后台执行线程
//    按列表顺序执行：坐标项直接点，找图项先抓屏匹配再点命中处。
//    所有等待都做成分片轮询（每片 ≤25ms），保证“停止”按下去立刻有反应。
// ===========================================================================
#include "app.h"

#include <thread>
#include <cmath>

EngineStatus g_status;

static std::thread        g_thread;
static std::atomic<bool>  g_stop{false};
static std::atomic<bool>  g_running{false};
static HWND               g_notify = nullptr;

static void SetStatus(const std::wstring& t, int step, int total) {
    std::lock_guard<std::mutex> lk(g_status.mtx);
    g_status.text = t;
    g_status.step = step;
    g_status.total = total;
}

// 分片睡眠：停止信号一来最多 25ms 就醒
static bool SleepInterruptible(int ms) {
    int remain = ms;
    while (remain > 0) {
        if (g_stop.load()) return false;
        const int chunk = remain > 25 ? 25 : remain;
        Sleep((DWORD)chunk);
        remain -= chunk;
    }
    return !g_stop.load();
}

// ---------------------------------------------------------------------------
// 模拟点击：SendInput 走的是系统输入队列，和目标程序“真人操作”收到的完全一样
// ---------------------------------------------------------------------------
static void SendMouseButton(DWORD downFlag, DWORD upFlag) {
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = downFlag;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = upFlag;
    UINT a = SendInput(1, &in[0], sizeof(INPUT));
    Sleep(25);                       // 按下与抬起之间留一点，太短部分程序会漏掉
    UINT b = SendInput(1, &in[1], sizeof(INPUT));
    if (a != 1 || b != 1)
        Log(L"SendInput 按下/抬起 被拒绝: %u / %u (错误码 %lu)", a, b, GetLastError());
}

static void SendClick(int x, int y, ClickKind kind) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw < 2 || vh < 2) return;

    // 绝对坐标要归一化到 0..65535 的虚拟桌面比例
    const LONG nx = (LONG)llround((double)(x - vx) * 65535.0 / (double)(vw - 1));
    const LONG ny = (LONG)llround((double)(y - vy) * 65535.0 / (double)(vh - 1));

    INPUT mv = {};
    mv.type = INPUT_MOUSE;
    mv.mi.dx = nx;
    mv.mi.dy = ny;
    mv.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    UINT m = SendInput(1, &mv, sizeof(INPUT));
    if (m != 1) Log(L"SendInput 移动被拒绝: %u (错误码 %lu) 目标 (%d,%d)", m, GetLastError(), x, y);
    Sleep(20);                       // 先到位再按，某些程序靠 hover 状态才认点击

    switch (kind) {
        case ClickKind::RightSingle:
            SendMouseButton(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
            break;
        case ClickKind::MiddleSingle:
            SendMouseButton(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP);
            break;
        case ClickKind::LeftDouble:
            SendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
            Sleep(45);
            SendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
            break;
        case ClickKind::LeftSingle:
        default:
            SendMouseButton(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
            break;
    }
    Sleep(30);                       // 抬键后再缓一下，避免下一步立刻抓屏抓到旧画面
}

// ---------------------------------------------------------------------------
// 工作线程
// ---------------------------------------------------------------------------
static void Worker(std::vector<TaskItem> items, RunSettings rs) {
    Log(L"Worker 线程启动，收到 %d 项", (int)items.size());
    std::vector<TaskItem> live;
    for (const TaskItem& it : items) if (it.enabled) live.push_back(it);
    Log(L"其中启用 %d 项，开始前延时 %d ms", (int)live.size(), rs.startDelayMs);

    if (live.empty()) {
        SetStatus(L"没有启用中的步骤 —— 请先添加或勾选列表项", 0, 0);
        g_running = false;
        if (g_notify) PostMessageW(g_notify, WM_APP_ENGINE_DONE, 0, 0);
        return;
    }

    const int total = (int)live.size();
    const bool infinite = (rs.loopCount == 0);
    if (!SleepInterruptible(rs.startDelayMs)) {
        Log(L"开始前延时期间收到停止信号");
        SetStatus(L"已停止", 0, total);
        g_running = false;
        if (g_notify) PostMessageW(g_notify, WM_APP_ENGINE_DONE, 0, 0);
        return;
    }

    int loop = 0;
    bool aborted = false;
    bool completed = false;
    double matchMsTotal = 0.0;
    int    matchCount = 0;
    int    clickCount = 0;

    while (!aborted && !g_stop.load()) {
        if (!infinite && loop >= rs.loopCount) { completed = true; break; }
        ++loop;
        if (!infinite && rs.loopCount > 1)
            SetStatus(Format(L"第 %d/%d 轮开始…", loop, rs.loopCount), 0, total);

        for (int i = 0; i < total && !aborted; ++i) {
            if (g_stop.load()) break;
            const TaskItem& it = live[(size_t)i];

            if (it.kind == ItemKind::Point) {
                Log(L"第 %d/%d 步：点击坐标 (%d,%d) 方式 %d", i + 1, total, it.x, it.y, (int)it.click);
                SetStatus(Format(L"第 %d/%d 步：点击坐标 (%d, %d)", i + 1, total, it.x, it.y),
                          i + 1, total);
                SendClick(it.x, it.y, it.click);
                ++clickCount;
            } else {
                auto tpl = GetTemplate(it.image);
                if (!tpl->ok) {
                    SetStatus(Format(L"第 %d/%d 步：模板图读不出来，已跳过（%s）",
                                     i + 1, total, FileNameOf(it.image).c_str()), i + 1, total);
                    if (!SleepInterruptible(rs.stepDelayMs)) break;
                    continue;
                }

                ScreenShot sc;
                MatchResult mr;
                if (!CaptureScreen(sc)) {
                    SetStatus(L"抓屏失败，已停止（可能被安全软件拦住）", i + 1, total);
                    aborted = true;
                    break;
                }
                SetStatus(Format(L"第 %d/%d 步：正在找图 %s …", i + 1, total,
                                 FileNameOf(it.image).c_str()), i + 1, total);
                mr = FindTemplate(sc, *tpl, it.similarity);
                matchMsTotal += mr.ms;
                ++matchCount;

                int tries = 0;
                while (!mr.found && rs.onNotFound == NotFoundAction::Retry &&
                       tries < rs.retryCount && !g_stop.load()) {
                    ++tries;
                    SetStatus(Format(L"第 %d/%d 步：没找到，重试 %d/%d …", i + 1, total,
                                     tries, rs.retryCount), i + 1, total);
                    if (!SleepInterruptible(rs.retryIntervalMs)) break;
                    if (!CaptureScreen(sc)) break;
                    mr = FindTemplate(sc, *tpl, it.similarity);
                    matchMsTotal += mr.ms;
                    ++matchCount;
                }
                if (g_stop.load()) break;

                if (mr.found) {
                    SetStatus(Format(L"第 %d/%d 步：命中 %s（相似度 %.1f%%，耗时 %.0fms）→ 点击 (%d, %d)",
                                     i + 1, total, FileNameOf(it.image).c_str(),
                                     mr.score * 100.0, mr.ms, mr.cx, mr.cy), i + 1, total);
                    SendClick(mr.cx, mr.cy, it.click);
                    ++clickCount;
                } else {
                    if (rs.onNotFound == NotFoundAction::Stop) {
                        SetStatus(Format(L"第 %d/%d 步：找不到 %s（最高只有 %.1f%%，阈值 %d%%）—— 已停止",
                                         i + 1, total, FileNameOf(it.image).c_str(),
                                         mr.score * 100.0, it.similarity), i + 1, total);
                        aborted = true;
                        break;
                    }
                    const int maxPct = (int)(mr.score * 100.0);
                    SetStatus(Format(L"第 %d/%d 步：没找到 %s（最高 %.1f%%，阈值 %d%%）→ 跳过",
                                     i + 1, total, FileNameOf(it.image).c_str(),
                                     mr.score * 100.0, it.similarity), i + 1, total);
                    (void)maxPct;
                }
            }

            if (!SleepInterruptible(rs.stepDelayMs)) break;
        }
    }

    if (aborted)          SetStatus(L"已停止", 0, total);
    else if (g_stop.load()) SetStatus(L"已停止", 0, total);
    else if (completed) {
        std::wstring extra;
        if (matchCount > 0)
            extra = Format(L"，找图 %d 次平均 %.0f ms", matchCount, matchMsTotal / matchCount);
        SetStatus(Format(L"执行完成：%d 步 × %d 轮，共点击 %d 次%s",
                         total, loop, clickCount, extra.c_str()), 0, total);
    }
    else                  SetStatus(L"已停止", 0, total);

    g_running = false;
    if (g_notify) PostMessageW(g_notify, WM_APP_ENGINE_DONE, 0, 0);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
bool EngineRunning() { return g_running.load(); }

bool EngineStart(HWND notify, const std::vector<TaskItem>& items, const RunSettings& rs) {
    if (g_running.load()) return false;
    EngineJoinIfNeeded();                 // 上一轮的线程对象要先收干净
    g_stop = false;
    g_notify = notify;
    g_running = true;
    {
        std::lock_guard<std::mutex> lk(g_status.mtx);
        g_status.running = true;
        g_status.text = L"准备开始…";
        g_status.step = 0;
        g_status.total = (int)items.size();
    }
    // 参数按值拷贝进线程，之后 UI 怎么改都不影响这一轮
    g_thread = std::thread(Worker, items, rs);
    return true;
}

void EngineStop() { g_stop = true; }

void EngineJoinIfNeeded() {
    if (g_thread.joinable()) g_thread.join();
}
