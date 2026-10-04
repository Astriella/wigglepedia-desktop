#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "assets_hole.h"
#include "assets_worm.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <windowsx.h>
#else
  #include <X11/Xlib.h>
  #include <X11/Xutil.h>
  #include <X11/keysym.h>
#endif

namespace cfg {
    constexpr int WIN_W = 480;
    constexpr int WIN_H = 620;
    constexpr int GRID  = 3;
    constexpr int CELL  = 130;
    constexpr int GAP   = 8;
    constexpr int OFF_X = (WIN_W - (GRID * CELL + (GRID - 1) * GAP)) / 2;
    constexpr int OFF_Y = 150;
    constexpr int MAX_POPS    = 10;
    constexpr int MIN_SHOW_MS = 1000;
    constexpr int MAX_SHOW_MS = 1500;
    constexpr const char* HIGHSCORE_FILE = "wigglepedia_highscore.txt";
}

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
};

struct Hole {
    bool active = false;
    int durationMs = 0;
    std::chrono::steady_clock::time_point shownAt;
};

enum class Scene { Menu, Playing, GameOver };

struct Game {
    Scene scene = Scene::Menu;
    std::vector<Hole> holes = std::vector<Hole>(cfg::GRID * cfg::GRID);
    int score = 0;
    int pops = 0;
    int activeIndex = -1;
    int lastIndex = -1;
    int highscore = 0;
    bool newRecord = false;
};

static Image gHole, gWorm;
static Game gGame;

static int loadHighscore() {
    std::ifstream in(cfg::HIGHSCORE_FILE);
    int hs = 0;
    if (in) in >> hs;
    return hs;
}

static void saveHighscore(int hs) {
    std::ofstream out(cfg::HIGHSCORE_FILE);
    if (out) out << hs;
}

static Image decodePng(const unsigned char* data, unsigned int len) {
    Image img;
    int n = 0;
    unsigned char* pixels = stbi_load_from_memory(
        data, static_cast<int>(len), &img.w, &img.h, &n, 4);
    if (!pixels) return img;
    img.px.resize(static_cast<size_t>(img.w) * img.h);
    for (int i = 0; i < img.w * img.h; ++i) {
        uint8_t r = pixels[i * 4 + 0];
        uint8_t g = pixels[i * 4 + 1];
        uint8_t b = pixels[i * 4 + 2];
        uint8_t a = pixels[i * 4 + 3];
        img.px[i] = (static_cast<uint32_t>(a) << 24) |
                    (static_cast<uint32_t>(r) << 16) |
                    (static_cast<uint32_t>(g) << 8)  |
                    (static_cast<uint32_t>(b));
    }
    stbi_image_free(pixels);
    return img;
}

static int randomInt(int min, int max) {
    static std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> dist(min, max - 1);
    return dist(rng);
}

static void resetGame() {
    gGame.score = 0;
    gGame.pops = 0;
    gGame.activeIndex = -1;
    gGame.lastIndex = -1;
    gGame.newRecord = false;
    for (auto& h : gGame.holes) { h.active = false; h.durationMs = 0; }
}

static void spawnWorm() {
    if (gGame.pops >= cfg::MAX_POPS) return;
    int idx;
    do { idx = randomInt(0, static_cast<int>(gGame.holes.size())); }
    while (idx == gGame.lastIndex && gGame.holes.size() > 1);
    gGame.lastIndex = idx;
    gGame.activeIndex = idx;
    gGame.holes[idx].active = true;
    gGame.holes[idx].durationMs = randomInt(cfg::MIN_SHOW_MS, cfg::MAX_SHOW_MS);
    gGame.holes[idx].shownAt = std::chrono::steady_clock::now();
    gGame.pops++;
}

static void updateGame() {
    if (gGame.scene != Scene::Playing) return;

    auto now = std::chrono::steady_clock::now();

    if (gGame.activeIndex == -1 && gGame.pops < cfg::MAX_POPS) {
        spawnWorm();
    }

    if (gGame.activeIndex != -1) {
        auto& h = gGame.holes[gGame.activeIndex];
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - h.shownAt).count();
        if (elapsed >= h.durationMs) {
            h.active = false;
            gGame.activeIndex = -1;
        }
    }

    if (gGame.pops >= cfg::MAX_POPS && gGame.activeIndex == -1) {
        gGame.newRecord = gGame.score > gGame.highscore;
        if (gGame.newRecord) {
            gGame.highscore = gGame.score;
            saveHighscore(gGame.highscore);
        }
        gGame.scene = Scene::GameOver;
    }
}

static int holeIndexAt(int mx, int my) {
    for (int r = 0; r < cfg::GRID; ++r) {
        for (int c = 0; c < cfg::GRID; ++c) {
            int x = cfg::OFF_X + c * (cfg::CELL + cfg::GAP);
            int y = cfg::OFF_Y + r * (cfg::CELL + cfg::GAP);
            if (mx >= x && mx < x + cfg::CELL && my >= y && my < y + cfg::CELL)
                return r * cfg::GRID + c;
        }
    }
    return -1;
}

static bool inStartButton(int mx, int my) {
    return mx >= cfg::WIN_W / 2 - 120 && mx <= cfg::WIN_W / 2 + 120 &&
           my >= 400 && my <= 460;
}

static bool inAgainButton(int mx, int my) {
    return mx >= cfg::WIN_W / 2 - 120 && mx <= cfg::WIN_W / 2 + 120 &&
           my >= 440 && my <= 500;
}

static void handleClick(int mx, int my) {
    if (gGame.scene == Scene::Menu && inStartButton(mx, my)) {
        resetGame();
        gGame.scene = Scene::Playing;
    } else if (gGame.scene == Scene::GameOver && inAgainButton(mx, my)) {
        resetGame();
        gGame.scene = Scene::Playing;
    } else if (gGame.scene == Scene::Playing) {
        int idx = holeIndexAt(mx, my);
        if (idx >= 0 && gGame.holes[idx].active) {
            gGame.score++;
            gGame.holes[idx].active = false;
            if (gGame.activeIndex == idx) gGame.activeIndex = -1;
        }
    }
}

static void fillRect(uint32_t* buf, int bufW, int bufH,
                     int x0, int y0, int w, int h, uint32_t color) {
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > bufW) w = bufW - x0;
    if (y0 + h > bufH) h = bufH - y0;
    if (w <= 0 || h <= 0) return;
    for (int y = y0; y < y0 + h; ++y) {
        uint32_t* row = buf + static_cast<size_t>(y) * bufW + x0;
        for (int x = 0; x < w; ++x) row[x] = color;
    }
}

static void drawRectOutline(uint32_t* buf, int bufW, int bufH,
                            int x0, int y0, int w, int h, int t, uint32_t color) {
    fillRect(buf, bufW, bufH, x0, y0, w, t, color);
    fillRect(buf, bufW, bufH, x0, y0 + h - t, w, t, color);
    fillRect(buf, bufW, bufH, x0, y0, t, h, color);
    fillRect(buf, bufW, bufH, x0 + w - t, y0, t, h, color);
}

static void drawImage(uint32_t* buf, int bufW, int bufH,
                      const Image& img, int dx, int dy, int dw, int dh) {
    if (img.w <= 0 || img.h <= 0 || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * img.h / dh;
        int ty = dy + y;
        if (ty < 0 || ty >= bufH) continue;
        for (int x = 0; x < dw; ++x) {
            int sx = x * img.w / dw;
            int tx = dx + x;
            if (tx < 0 || tx >= bufW) continue;

            uint32_t src = img.px[static_cast<size_t>(sy) * img.w + sx];
            uint8_t a = (src >> 24) & 0xFF;
            if (a == 0) continue;

            uint32_t& dst = buf[static_cast<size_t>(ty) * bufW + tx];
            if (a == 255) {
                dst = src;
            } else {
                uint8_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
                uint8_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                uint8_t r = static_cast<uint8_t>((sr * a + dr * (255 - a)) / 255);
                uint8_t g = static_cast<uint8_t>((sg * a + dg * (255 - a)) / 255);
                uint8_t b = static_cast<uint8_t>((sb * a + db * (255 - a)) / 255);
                dst = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }
}

struct Glyph {
    uint8_t rows[7];
};

static const Glyph* glyphFor(char c) {
    static const Glyph FONT[43] = {
        {{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, // A
        {{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}}, // B
        {{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, // C
        {{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}}, // D
        {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, // E
        {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}}, // F
        {{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, // G
        {{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}}, // H
        {{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, // I
        {{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}}, // J
        {{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, // K
        {{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}}, // L
        {{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, // M
        {{0x11,0x19,0x15,0x13,0x11,0x11,0x11}}, // N
        {{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, // O
        {{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}}, // P
        {{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, // Q
        {{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}}, // R
        {{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, // S
        {{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}}, // T
        {{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, // U
        {{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}}, // V
        {{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}}, // W
        {{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}}, // X
        {{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, // Y
        {{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}}, // Z
        {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, // 0
        {{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}}, // 1
        {{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, // 2
        {{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}}, // 3
        {{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, // 4
        {{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}}, // 5
        {{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, // 6
        {{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}}, // 7
        {{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, // 8
        {{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}}, // 9
        {{0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, // space
        {{0x00,0x00,0x00,0x00,0x00,0x00,0x04}}, // .
        {{0x00,0x04,0x04,0x00,0x04,0x04,0x00}}, // :
        {{0x04,0x04,0x04,0x04,0x04,0x00,0x04}}, // !
        {{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}}, // =
        {{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}}, // ?
        {{0x00,0x00,0x04,0x00,0x04,0x00,0x00}}  // /
    };

    if (c >= 'A' && c <= 'Z') return &FONT[c - 'A'];
    if (c >= 'a' && c <= 'z') return &FONT[c - 'a'];
    if (c >= '0' && c <= '9') return &FONT[26 + (c - '0')];
    if (c == ' ') return &FONT[36];
    if (c == '.') return &FONT[37];
    if (c == ':') return &FONT[38];
    if (c == '!') return &FONT[39];
    if (c == '=') return &FONT[40];
    if (c == '?') return &FONT[41];
    if (c == '/') return &FONT[42];
    return nullptr;
}

static int textWidth(const std::string& s, int scale) {
    return static_cast<int>(s.size()) * 6 * scale - scale;
}

static void drawText(uint32_t* buf, int bufW, int bufH,
                     const std::string& text, int cx, int cy,
                     int scale, uint32_t color) {
    int cursorX = cx - textWidth(text, scale) / 2;
    for (char ch : text) {
        const Glyph* g = glyphFor(ch);
        if (g) {
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (g->rows[row] & (1 << (4 - col))) {
                        fillRect(buf, bufW, bufH,
                                 cursorX + col * scale,
                                 cy + row * scale,
                                 scale, scale, color);
                    }
                }
            }
        }
        cursorX += 6 * scale;
    }
}

static void renderFrame(std::vector<uint32_t>& buffer) {
    const uint32_t bg       = 0xFF004304u;
    const uint32_t white    = 0xFFFFFFFFu;
    const uint32_t btn      = 0xFFB48C5Au;
    const uint32_t btnEdge  = 0xFFFFFFFFu;

    int W = cfg::WIN_W, H = cfg::WIN_H;
    buffer.assign(static_cast<size_t>(W) * H, bg);

    if (gGame.scene == Scene::Menu) {
        drawText(buffer.data(), W, H, "WIGGLEPEDIA", W / 2, 110, 5, white);
        drawText(buffer.data(), W, H, "WHACK THE WORMS", W / 2, 210, 2, white);
        drawText(buffer.data(), W, H,
                 "HIGHSCORE " + std::to_string(gGame.highscore),
                 W / 2, 260, 2, white);
        drawImage(buffer.data(), W, H, gWorm, W / 2 - 40, 300, 80, 80);

        fillRect(buffer.data(), W, H, W / 2 - 120, 400, 240, 60, btn);
        drawRectOutline(buffer.data(), W, H, W / 2 - 120, 400, 240, 60, 3, btnEdge);
        drawText(buffer.data(), W, H, "START", W / 2, 420, 4, white);
    }
    else if (gGame.scene == Scene::Playing) {
        for (int r = 0; r < cfg::GRID; ++r) {
            for (int c = 0; c < cfg::GRID; ++c) {
                int x = cfg::OFF_X + c * (cfg::CELL + cfg::GAP);
                int y = cfg::OFF_Y + r * (cfg::CELL + cfg::GAP);
                drawImage(buffer.data(), W, H, gHole, x, y, cfg::CELL, cfg::CELL);
            }
        }

        for (int i = 0; i < static_cast<int>(gGame.holes.size()); ++i) {
            if (!gGame.holes[i].active) continue;
            int r = i / cfg::GRID, c = i % cfg::GRID;
            int x = cfg::OFF_X + c * (cfg::CELL + cfg::GAP);
            int y = cfg::OFF_Y + r * (cfg::CELL + cfg::GAP);
            int wsz = cfg::CELL * 6 / 10;
            int off = (cfg::CELL - wsz) / 2;
            drawImage(buffer.data(), W, H, gWorm, x + off, y + off, wsz, wsz);
        }

        drawText(buffer.data(), W, H,
                 "HITS " + std::to_string(gGame.score),
                 W / 2, 40, 4, white);
        drawText(buffer.data(), W, H,
                 "POPS " + std::to_string(gGame.pops) + "/" + std::to_string(cfg::MAX_POPS),
                 W / 2, 90, 2, white);
        drawText(buffer.data(), W, H,
                 "HIGHSCORE " + std::to_string(gGame.highscore),
                 W / 2, 115, 2, white);
    }
    else {
        drawText(buffer.data(), W, H, "GAME OVER", W / 2, 140, 5, white);
        drawText(buffer.data(), W, H,
                 "HITS " + std::to_string(gGame.score) + "/" + std::to_string(cfg::MAX_POPS),
                 W / 2, 240, 2, white);
        drawText(buffer.data(), W, H,
                 "HIGHSCORE " + std::to_string(gGame.highscore),
                 W / 2, 280, 2, white);
        if (gGame.newRecord) {
            drawText(buffer.data(), W, H, "NEW HIGHSCORE!", W / 2, 330, 2, white);
        }
        fillRect(buffer.data(), W, H, W / 2 - 120, 440, 240, 60, btn);
        drawRectOutline(buffer.data(), W, H, W / 2 - 120, 440, 240, 60, 3, btnEdge);
        drawText(buffer.data(), W, H, "PLAY AGAIN", W / 2, 460, 3, white);
    }
}

#ifdef _WIN32

static std::vector<uint32_t> gBackbuffer;
static BITMAPINFO gBmi{};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, 1, 16, nullptr);
            return 0;

        case WM_TIMER:
            updateGame();
            renderFrame(gBackbuffer);
            {
                HDC hdc = GetDC(hwnd);
                gBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                gBmi.bmiHeader.biWidth = cfg::WIN_W;
                gBmi.bmiHeader.biHeight = -cfg::WIN_H;
                gBmi.bmiHeader.biPlanes = 1;
                gBmi.bmiHeader.biBitCount = 32;
                gBmi.bmiHeader.biCompression = BI_RGB;
                StretchDIBits(hdc, 0, 0, cfg::WIN_W, cfg::WIN_H,
                              0, 0, cfg::WIN_W, cfg::WIN_H,
                              gBackbuffer.data(), &gBmi, DIB_RGB_COLORS, SRCCOPY);
                ReleaseDC(hwnd, hdc);
            }
            return 0;

        case WM_LBUTTONDOWN:
            handleClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) PostQuitMessage(0);
            if (wp == VK_RETURN && gGame.scene == Scene::Menu) {
                resetGame();
                gGame.scene = Scene::Playing;
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    gHole = decodePng(hole_png, hole_png_len);
    gWorm = decodePng(worm_png, worm_png_len);
    gGame.highscore = loadHighscore();

    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "WigglepediaWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClass(&wc);

    RECT rc{0, 0, cfg::WIN_W, cfg::WIN_H};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowEx(0, "WigglepediaWnd", "Wigglepedia",
                               style, CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top,
                               nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

#else

static std::vector<uint32_t> gBackbuffer;
static Display* gDisplay = nullptr;
static Window gWindow = 0;
static Atom gWmDelete = 0;
static XImage* gXImage = nullptr;

static void presentX11() {
    GC gc = DefaultGC(gDisplay, DefaultScreen(gDisplay));
    XPutImage(gDisplay, gWindow, gc, gXImage, 0, 0, 0, 0, cfg::WIN_W, cfg::WIN_H);
    XFlush(gDisplay);
}

int main() {
    gHole = decodePng(hole_png, hole_png_len);
    gWorm = decodePng(worm_png, worm_png_len);
    gGame.highscore = loadHighscore();

    gDisplay = XOpenDisplay(nullptr);
    if (!gDisplay) return 1;

    int screen = DefaultScreen(gDisplay);
    gWindow = XCreateSimpleWindow(gDisplay, RootWindow(gDisplay, screen),
                                  0, 0, cfg::WIN_W, cfg::WIN_H, 0,
                                  BlackPixel(gDisplay, screen),
                                  BlackPixel(gDisplay, screen));
    XStoreName(gDisplay, gWindow, "Wigglepedia");
    XSelectInput(gDisplay, gWindow, ExposureMask | ButtonPressMask | KeyPressMask);
    XMapWindow(gDisplay, gWindow);

    gWmDelete = XInternAtom(gDisplay, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(gDisplay, gWindow, &gWmDelete, 1);

    gBackbuffer.assign(static_cast<size_t>(cfg::WIN_W) * cfg::WIN_H, 0);
    gXImage = XCreateImage(gDisplay, DefaultVisual(gDisplay, screen),
                           DefaultDepth(gDisplay, screen), ZPixmap, 0,
                           reinterpret_cast<char*>(gBackbuffer.data()),
                           cfg::WIN_W, cfg::WIN_H, 32, 0);

    auto lastTick = std::chrono::steady_clock::now();

    while (true) {
        while (XPending(gDisplay)) {
            XEvent ev;
            XNextEvent(gDisplay, &ev);
            if (ev.type == ClientMessage &&
                static_cast<Atom>(ev.xclient.data.l[0]) == gWmDelete) {
                XDestroyImage(gXImage);
                XCloseDisplay(gDisplay);
                return 0;
            }
            if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
                handleClick(ev.xbutton.x, ev.xbutton.y);
            }
            if (ev.type == KeyPress) {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_Escape) {
                    XDestroyImage(gXImage);
                    XCloseDisplay(gDisplay);
                    return 0;
                }
                if (ks == XK_Return && gGame.scene == Scene::Menu) {
                    resetGame();
                    gGame.scene = Scene::Playing;
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastTick).count() >= 16) {
            lastTick = now;
            updateGame();
            renderFrame(gBackbuffer);
            presentX11();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

#endif