// ===========================================================================
//  imaging.cpp  --  屏幕抓取、模板匹配引擎、缩略图与 PNG 保存
//
//  匹配算法分两级，兼顾速度与准确：
//    ① 粗筛：灰度 SAD，SSE2 一次比 16 像素，逐行提前终止，多线程分片。
//       只保留“足够接近”的若干候选位置。
//    ② 精算：对候选位置算 ZNCC（去均值归一化互相关）。
//       ZNCC 对整体明暗变化不敏感，而且当屏幕那块区域是一片纯色时，
//       分数会天然变低 —— 这正是防止“点在空白处”的关键。
// ===========================================================================
#include "app.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <map>
#include <emmintrin.h>

// ---------------------------------------------------------------------------
// 抓屏
// ---------------------------------------------------------------------------
bool CaptureScreen(ScreenShot& out) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) { ReleaseDC(nullptr, screen); return false; }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = vw;
    bi.bmiHeader.biHeight      = -vh;          // 负高度 = top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        if (BitBlt(mem, 0, 0, vw, vh, screen, vx, vy, SRCCOPY | CAPTUREBLT)) {
            GdiFlush();                        // 必须：让 GDI 把像素真正写进 DIB
            out.w = vw; out.h = vh; out.left = vx; out.top = vy;
            out.px.assign((size_t)vw * vh, 0u);
            memcpy(out.px.data(), bits, (size_t)vw * vh * 4);
            ok = true;
        }
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

// 由像素缓冲建 32bpp top-down DIB。dimPercent>0 时整体压暗，用于截图覆盖层的“未选中”区域。
static HBITMAP CreateDib(const uint32_t* px, int w, int h, int dimPercent, HDC* outMemDC) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        uint32_t* dst = (uint32_t*)bits;
        if (dimPercent <= 0) {
            memcpy(dst, px, (size_t)w * h * 4);
        } else {
            const int keep = 100 - (dimPercent > 90 ? 90 : dimPercent);
            for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
                uint32_t v = px[i];
                uint32_t b = ((v      ) & 0xFF) * keep / 100;
                uint32_t g = ((v >>  8) & 0xFF) * keep / 100;
                uint32_t r = ((v >> 16) & 0xFF) * keep / 100;
                dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
        GdiFlush();
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (outMemDC) {
        HDC m2 = CreateCompatibleDC(nullptr);
        SelectObject(m2, dib);
        *outMemDC = m2;
    }
    return dib;
}

Gdiplus::Bitmap* ScreenToBitmap(const ScreenShot& sc) {
    if (!sc.valid()) return nullptr;
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(sc.w, sc.h, PixelFormat32bppRGB);
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return nullptr; }
    Gdiplus::Rect rc(0, 0, sc.w, sc.h);
    Gdiplus::BitmapData bd = {};
    if (bmp->LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppRGB, &bd) != Gdiplus::Ok) {
        delete bmp; return nullptr;
    }
    for (int y = 0; y < sc.h; ++y) {
        memcpy((uint8_t*)bd.Scan0 + (size_t)y * bd.Stride,
               sc.px.data() + (size_t)y * sc.w, (size_t)sc.w * 4);
    }
    bmp->UnlockBits(&bd);
    return bmp;
}

// ---------------------------------------------------------------------------
// PNG 保存
// ---------------------------------------------------------------------------
static int GetEncoderClsid(const WCHAR* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return -1;
    std::vector<uint8_t> buf(size);
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)buf.data();
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, mime) == 0) { *clsid = info[i].Clsid; return (int)i; }
    }
    return -1;
}

bool SavePngRgba(const uint32_t* px, int w, int h, const std::wstring& path) {
    if (!px || w <= 0 || h <= 0) return false;
    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppRGB);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    Gdiplus::Rect rc(0, 0, w, h);
    Gdiplus::BitmapData bd = {};
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppRGB, &bd) != Gdiplus::Ok)
        return false;
    for (int y = 0; y < h; ++y)
        memcpy((uint8_t*)bd.Scan0 + (size_t)y * bd.Stride, px + (size_t)y * w, (size_t)w * 4);
    bmp.UnlockBits(&bd);

    CLSID png = {};
    if (GetEncoderClsid(L"image/png", &png) < 0) return false;
    return bmp.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
}

// ---------------------------------------------------------------------------
// 模板载入与缓存
// ---------------------------------------------------------------------------
std::shared_ptr<TemplateImage> LoadTemplate(const std::wstring& path) {
    auto t = std::make_shared<TemplateImage>();
    t->path = path;

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(path.c_str());
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return t; }
    const int w = (int)bmp->GetWidth(), h = (int)bmp->GetHeight();
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { delete bmp; return t; }

    Gdiplus::Rect rc(0, 0, w, h);
    Gdiplus::BitmapData bd = {};
    if (bmp->LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppRGB, &bd) != Gdiplus::Ok) {
        delete bmp; return t;
    }
    t->w = w; t->h = h;
    t->gray.assign((size_t)w * h, 0);
    double sum = 0, sum2 = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = (const uint8_t*)bd.Scan0 + (size_t)y * bd.Stride;
        uint8_t* dst = t->gray.data() + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            const uint8_t b = row[x * 4 + 0], g = row[x * 4 + 1], r = row[x * 4 + 2];
            const uint8_t v = (uint8_t)((r * 77 + g * 151 + b * 28) >> 8);
            dst[x] = v;
            sum += v; sum2 += (double)v * v;
        }
    }
    bmp->UnlockBits(&bd);
    delete bmp;

    const double n = (double)w * h;
    t->mean   = sum / n;
    t->varSum = sum2 - n * t->mean * t->mean;
    if (t->varSum < 0) t->varSum = 0;
    t->ok = true;
    return t;
}

static std::mutex g_tplMtx;
static std::map<std::wstring, std::shared_ptr<TemplateImage>> g_tplCache;

std::shared_ptr<TemplateImage> GetTemplate(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lk(g_tplMtx);
        auto it = g_tplCache.find(path);
        if (it != g_tplCache.end()) return it->second;
    }
    auto t = LoadTemplate(path);
    std::lock_guard<std::mutex> lk(g_tplMtx);
    auto it = g_tplCache.find(path);
    if (it != g_tplCache.end()) return it->second;    // 别的线程先放进去了
    g_tplCache[path] = t;
    return t;
}

// ---------------------------------------------------------------------------
// 缩略图缓存
// ---------------------------------------------------------------------------
static std::mutex g_thumbMtx;
static std::map<std::wstring, Gdiplus::Bitmap*> g_thumbCache;

Gdiplus::Bitmap* GetThumb(const std::wstring& path, int maxW, int maxH) {
    std::wstring key = path + L"|" + std::to_wstring(maxW) + L"x" + std::to_wstring(maxH);
    {
        std::lock_guard<std::mutex> lk(g_thumbMtx);
        auto it = g_thumbCache.find(key);
        if (it != g_thumbCache.end()) return it->second;
    }
    Gdiplus::Bitmap* out = nullptr;
    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(path.c_str());
    if (src && src->GetLastStatus() == Gdiplus::Ok) {
        const int sw = (int)src->GetWidth(), sh = (int)src->GetHeight();
        if (sw > 0 && sh > 0) {
            double k = std::min((double)maxW / sw, (double)maxH / sh);
            if (k > 1.0) k = 1.0;
            int tw = (int)(sw * k + 0.5), th = (int)(sh * k + 0.5);
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
            Gdiplus::Bitmap* thumb = new Gdiplus::Bitmap(tw, th, PixelFormat32bppPARGB);
            if (thumb->GetLastStatus() == Gdiplus::Ok) {
                Gdiplus::Graphics g(thumb);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                g.DrawImage(src, Gdiplus::Rect(0, 0, tw, th));
                out = thumb;
            } else {
                delete thumb;
            }
        }
    }
    delete src;

    std::lock_guard<std::mutex> lk(g_thumbMtx);
    g_thumbCache[key] = out;   // 失败也缓存 nullptr，避免反复重试
    return out;
}

// ---------------------------------------------------------------------------
// 匹配引擎
// ---------------------------------------------------------------------------
#ifdef _M_X64
#define HAS_SSE2 1
#else
#define HAS_SSE2 0
#endif

static inline int SadRow(const uint8_t* a, const uint8_t* b, int n) {
    int i = 0, s = 0;
#if HAS_SSE2
    __m128i acc = _mm_setzero_si128();
    for (; i + 16 <= n; i += 16) {
        __m128i va = _mm_loadu_si128((const __m128i*)(a + i));
        __m128i vb = _mm_loadu_si128((const __m128i*)(b + i));
        acc = _mm_add_epi64(acc, _mm_sad_epu8(va, vb));
    }
    s = (int)(_mm_cvtsi128_si64(acc) + _mm_cvtsi128_si64(_mm_srli_si128(acc, 8)));
#endif
    for (; i < n; ++i) s += std::abs((int)a[i] - (int)b[i]);
    return s;
}

struct Cand { int x, y, sad; };

// 单线程在一段 y 范围里做粗筛，维护一个按 SAD 升序的“最优 K 名”小数组
//
// 关键在提前终止的判定方式：预算是“整张模板”的总差，但如果只在整个模板算完后才比，
// 一个明显不匹配的位置也要把 70 行全跑完才退出。这里改成**按行等比例**判定：
// 第 r 行结束时，累计差不得超过 (r+2) 份的单行份额 —— 即“等比例份额 + 一行宽限”。
// 这样正常匹配（每行都在份额内）不受影响，而不匹配的位置第一行就被淘汰。
static void ScanRange(const std::vector<uint8_t>& gs, int sw, int maxX, int y0, int y1,
                      const TemplateImage& t, std::vector<Cand>& best, int K, int hardLimit) {
    const int tw = t.w, th = t.h;
    const size_t tstride = (size_t)tw;

    for (int y = y0; y < y1; ++y) {
        const uint8_t* base = gs.data() + (size_t)y * sw;
        for (int x = 0; x <= maxX; ++x) {
            const uint8_t* s = base + x;
            int limit = hardLimit;
            if ((int)best.size() >= K) {
                int soft = best[K - 1].sad + tw * th;   // 允许再宽 1 级平均灰度
                if (soft < limit) limit = soft;
            }
            const int step = limit / th;
            int allowed = step + step;                  // 份额 + 一行宽限
            int sad = 0;
            const uint8_t* sp = s;
            const uint8_t* tp = t.gray.data();
            bool ok = true;
            for (int r = 0; r < th; ++r) {
                sad += SadRow(sp, tp, tw);
                if (sad > allowed) { ok = false; break; }
                allowed += step;
                sp += sw;
                tp += tstride;
            }
            if (!ok) continue;

            // 插入有序数组
            if ((int)best.size() < K) {
                Cand c{ x, y, sad };
                auto pos = std::lower_bound(best.begin(), best.end(), c,
                                            [](const Cand& a, const Cand& b) { return a.sad < b.sad; });
                best.insert(pos, c);
            } else if (sad < best[K - 1].sad) {
                Cand c{ x, y, sad };
                auto pos = std::lower_bound(best.begin(), best.end(), c,
                                            [](const Cand& a, const Cand& b) { return a.sad < b.sad; });
                best.insert(pos, c);
                best.pop_back();
            }
        }
    }
}

// 去均值归一化互相关；返回 -1 表示该窗口没有对比度、结果不可信
static double ScoreZNCC(const std::vector<uint8_t>& gs, int sw, int x, int y, const TemplateImage& t) {
    const int tw = t.w, th = t.h;
    double sumI = 0, sumI2 = 0, sumIT = 0;
    for (int r = 0; r < th; ++r) {
        const uint8_t* s = gs.data() + (size_t)(y + r) * sw + x;
        const uint8_t* p = t.gray.data() + (size_t)r * tw;
        for (int c = 0; c < tw; ++c) {
            const double iv = s[c], tv = p[c];
            sumI  += iv;
            sumI2 += iv * iv;
            sumIT += iv * tv;
        }
    }
    const double n = (double)tw * th;
    const double meanI = sumI / n;
    const double varI = sumI2 - n * meanI * meanI;
    if (varI < 1.0 || t.varSum < 1.0) return -1.0;
    double cov = sumIT - n * meanI * t.mean;
    double score = cov / std::sqrt(varI * t.varSum);
    if (score > 1.0) score = 1.0;
    if (score < 0.0) score = 0.0;
    return score;
}

MatchResult FindTemplate(const ScreenShot& sc, const TemplateImage& tpl, int minScorePct) {
    MatchResult res;
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);

    if (!sc.valid() || !tpl.ok) return res;
    const int sw = sc.w, sh = sc.h, tw = tpl.w, th = tpl.h;
    if (tw > sw || th > sh) return res;

    // 全屏灰度化（每次扫描都要做：画面已经变了）
    LARGE_INTEGER g0, g1, s0, s1;
    QueryPerformanceCounter(&g0);
    std::vector<uint8_t> gs((size_t)sw * sh);
    for (int y = 0; y < sh; ++y) {
        const uint32_t* src = sc.px.data() + (size_t)y * sw;
        uint8_t* dst = gs.data() + (size_t)y * sw;
        for (int x = 0; x < sw; ++x) {
            const uint32_t v = src[x];
            dst[x] = (uint8_t)((((v >> 16) & 0xFF) * 77 + ((v >> 8) & 0xFF) * 151 +
                                (v & 0xFF) * 28) >> 8);
        }
    }

    const int maxX = sw - tw, maxY = sh - th;
    const int n = tw * th;
    const int hardLimit = n * 32;          // 平均每像素差 32 级以内都算候选
    QueryPerformanceCounter(&g1);

    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    if (hw > 16) hw = 16;
    const int nthreads = (int)hw;
    const int K = 24;

    std::vector<std::vector<Cand>> lists(nthreads);
    std::vector<std::thread> pool;
    const int rowsPer = (maxY + 1 + nthreads - 1) / nthreads;
    for (int i = 0; i < nthreads; ++i) {
        int y0 = i * rowsPer;
        int y1 = std::min(maxY + 1, y0 + rowsPer);
        if (y0 >= y1) break;
        pool.emplace_back([&, y0, y1, i]() {
            lists[i].reserve(K);
            ScanRange(gs, sw, maxX, y0, y1, tpl, lists[i], K, hardLimit);
        });
    }
    for (auto& th_ : pool) th_.join();
    QueryPerformanceCounter(&s0);

    std::vector<Cand> all;
    for (auto& l : lists) all.insert(all.end(), l.begin(), l.end());
    if (all.empty()) return res;
    std::sort(all.begin(), all.end(), [](const Cand& a, const Cand& b) { return a.sad < b.sad; });

    // 非极大值抑制：同一处目标只留一个代表
    std::vector<Cand> picks;
    const int minDist = std::max(3, std::min(tw, th) / 3);
    for (const Cand& c : all) {
        bool dup = false;
        for (const Cand& p : picks) {
            if (std::abs(p.x - c.x) < minDist && std::abs(p.y - c.y) < minDist) { dup = true; break; }
        }
        if (!dup) {
            picks.push_back(c);
            if (picks.size() >= 40) break;
        }
    }

    // 精算
    double bestScore = -1.0;
    int bx = 0, by = 0, bSad = 0;
    const bool lowContrast = (tpl.varSum < (double)n * 4.0);   // 模板本身几乎没花纹
    for (const Cand& c : picks) {
        double s = ScoreZNCC(gs, sw, c.x, c.y, tpl);
        if (s < 0.0 && lowContrast) {
            // 低对比度模板无法算相关，退回“平均灰度差”口径
            s = 1.0 - (double)c.sad / (255.0 * n);
        }
        if (s > bestScore) { bestScore = s; bx = c.x; by = c.y; bSad = c.sad; }
    }
    (void)bSad;

    QueryPerformanceCounter(&s1);
    const double msGray   = (double)(g1.QuadPart - g0.QuadPart) * 1000.0 / (double)f.QuadPart;
    const double msScan   = (double)(s0.QuadPart - g1.QuadPart) * 1000.0 / (double)f.QuadPart;
    const double msRefine = (double)(s1.QuadPart - s0.QuadPart) * 1000.0 / (double)f.QuadPart;

    res.scanned = (int)picks.size();
    res.x = sc.left + bx;
    res.y = sc.top  + by;
    res.cx = res.x + tw / 2;
    res.cy = res.y + th / 2;
    res.score = bestScore;
    res.found = (bestScore * 100.0 >= (double)minScorePct - 1e-6);

    QueryPerformanceCounter(&t1);
    res.ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    Log(L"找图 %dx%d 模板 %dx%d：候选 %d，用时 %.1fms（灰度 %.1f + 粗筛 %.1f + 精算 %.1f）",
        sw, sh, tw, th, res.scanned, res.ms, msGray, msScan, msRefine);
    return res;
}

// 供 overlay.cpp 使用
HBITMAP OverlayCreateDib(const uint32_t* px, int w, int h, int dimPercent, HDC* outMemDC) {
    return CreateDib(px, w, h, dimPercent, outMemDC);
}
