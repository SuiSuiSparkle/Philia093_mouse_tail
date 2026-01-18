#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

// 颜色定义
const SDL_Color COLOR_MAIN = {243, 186, 236, 255}; 
const SDL_Color COLOR_AUX = {125, 232, 243, 255};  

// 逻辑常量
const int TRAIL_LENGTH = 14;      // 拖尾长度 (减少以节省性能)
const int RIPPLE_MAX_LIFE = 30;   // 涟漪最大生存帧数 (减少)
const float RIPPLE_GROWTH = 2.0f; // 涟漪半径增长速度
const int CIRCLE_SEGMENTS = 24;   // 圆形近似分段数 (降至24，视觉差别小但更省性能)

struct Point { float x, y; };
struct TrailNode { Point pos; Uint64 timestamp; };
struct Ripple { Point center; float radius; float life; SDL_Color color; };

std::deque<TrailNode> trail;
std::vector<Ripple> ripples;
// 预计算单位圆顶点 (+1 用于闭合)
std::vector<SDL_FPoint> unitCircle(CIRCLE_SEGMENTS + 1);

struct AppState {
    bool running;
    bool visible;
};

// ...existing code...
// (保留原有的 QuitApp, ToggleVisibility, lerpColor 函数)
void SDLCALL QuitApp(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState*)userdata;
    state->running = false;
}

void SDLCALL ToggleVisibility(void *userdata, SDL_TrayEntry *entry) {
    AppState *state = (AppState*)userdata;
    state->visible = !state->visible;
    SDL_SetTrayEntryChecked(entry, state->visible);
}

SDL_Color lerpColor(SDL_Color c1, SDL_Color c2, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return SDL_Color{
        (Uint8)(c1.r + (c2.r - c1.r) * t),
        (Uint8)(c1.g + (c2.g - c1.g) * t),
        (Uint8)(c1.b + (c2.b - c1.b) * t),
        (Uint8)(c1.a + (c2.a - c1.a) * t)
    };
}

// 优化：使用 SDL_RenderLines 批量绘制
void drawCircleApprox(SDL_Renderer* renderer, float cx, float cy, float radius) {
    if (radius <= 0.5f) return;
    
    // 使用静态缓冲区减少内存分配
    static std::vector<SDL_FPoint> points(CIRCLE_SEGMENTS + 1);
    
    // 利用预计算的单位圆数据进行快速变换
    for (int i = 0; i <= CIRCLE_SEGMENTS; ++i) {
        points[i].x = cx + unitCircle[i].x * radius;
        points[i].y = cy + unitCircle[i].y * radius;
    }
    
    SDL_RenderLines(renderer, points.data(), (int)points.size());
}

int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL初始化失败: %s", SDL_GetError());
        return 1;
    }
    
    // 初始化预计算圆
    const float twoPi = 6.28318530718f;
    for (int i = 0; i <= CIRCLE_SEGMENTS; ++i) {
        float angle = (twoPi * i) / CIRCLE_SEGMENTS;
        unitCircle[i].x = cosf(angle);
        unitCircle[i].y = sinf(angle);
    }
    
    // 优化：设置渲染质量提示
    SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "0");

    SDL_DisplayID mainDisplay = SDL_GetPrimaryDisplay();
    SDL_Rect bounds;
    SDL_GetDisplayBounds(mainDisplay, &bounds);

    SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_UTILITY | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_TRANSPARENT;
    SDL_Window* window = SDL_CreateWindow("Philia093_line", bounds.w, bounds.h, flags);
    
    if (!window) {
        SDL_Log("窗口创建失败: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(window, bounds.x, bounds.y);

#ifdef _WIN32
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), "SDL.window.win32.hwnd", NULL);
    if (hwnd) {
        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
    }
#endif

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) return 1;
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    // 关闭垂直同步
    SDL_SetRenderVSync(renderer, 0);

    AppState appState = {true, true};
    SDL_Event event;
    
    // 托盘初始化
    // 如果没有找到文件，则生成一个默认的色块图标
    const char* iconPath = "philia093_smile.bmp";
    SDL_Surface* iconSurface = SDL_LoadBMP(iconPath); 
    
    if (!iconSurface) {
        // 如果加载文件失败，建立默认紫色方块
        iconSurface = SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_RGBA32);
        if (iconSurface) {
            SDL_FillSurfaceRect(iconSurface, NULL, SDL_MapRGBA(SDL_GetPixelFormatDetails(iconSurface->format), NULL, 243, 186, 236, 255));
        }
    }

    SDL_Tray* tray = NULL;
    if (iconSurface) {
        tray = SDL_CreateTray(NULL, "Mouse Overlay");
        if (tray) {
            SDL_SetTrayIcon(tray, iconSurface);
            SDL_TrayMenu* menu = SDL_CreateTrayMenu(tray);
            SDL_TrayEntry* toggleEntry = SDL_InsertTrayEntryAt(menu, -1, "Philia093_line state", SDL_TRAYENTRY_CHECKBOX);
            SDL_SetTrayEntryChecked(toggleEntry, true);
            SDL_SetTrayEntryCallback(toggleEntry, ToggleVisibility, &appState);
            // SDL_InsertTrayEntryAt(menu, -1, "-", SDL_TRAYENTRY_SEPARATOR); // 报错屏蔽，视情况启用
            SDL_TrayEntry* quitEntry = SDL_InsertTrayEntryAt(menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);
            SDL_SetTrayEntryCallback(quitEntry, QuitApp, &appState);
        }
        SDL_DestroySurface(iconSurface);
    }
    
    bool wasLeftDown = false;
    bool wasRightDown = false;

    // 修复：主循环逻辑重构
    while (appState.running) {
        // 事件处理
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                appState.running = false;
            }
        }

        float gx, gy;
        Uint32 buttons = SDL_GetGlobalMouseState(&gx, &gy);
        float wx = gx - bounds.x;
        float wy = gy - bounds.y;
        
        bool hasActivity = false; // 用于检测是否需要重绘
        Uint64 now = SDL_GetTicks();

        // 1. 更新轨迹 (仅当有位置变化时)
        if (trail.empty() || std::abs(trail.front().pos.x - wx) > 0.1f || std::abs(trail.front().pos.y - wy) > 0.1f) {
            trail.push_front({ {wx, wy}, now });
            hasActivity = true;
        }

        // 移除超时(>1s)或超过长度限制的点
        while (!trail.empty()) {
            bool expired = (now - trail.back().timestamp > 200);
            if (trail.size() > TRAIL_LENGTH || expired) {
                trail.pop_back();
                hasActivity = true;
            } else {
                break;
            }
        }

        // 2. 检测点击
        bool isLeftDown = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
        bool isRightDown = (buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;

        if (isLeftDown && !wasLeftDown) {
            // 左键：交替颜色
            static bool useAux = false;
            SDL_Color targetColor = useAux ? COLOR_AUX : COLOR_MAIN;
            useAux = !useAux;

            ripples.push_back({ {wx, wy}, 0.0f, (float)RIPPLE_MAX_LIFE, targetColor });
            hasActivity = true;
        }
        wasLeftDown = isLeftDown;

        if (isRightDown && !wasRightDown) {
            // 右键：固定主色
            ripples.push_back({ {wx, wy}, 0.0f, (float)RIPPLE_MAX_LIFE, COLOR_MAIN });
            hasActivity = true;
        }
        wasRightDown = isRightDown;

        // 3. 更新涟漪状态 & 移除死涟漪
        if (!ripples.empty()) {
            hasActivity = true;
            for (auto& r : ripples) {
                r.radius += RIPPLE_GROWTH;
                r.life -= 1.0f;
            }
            // 移除生命周期结束的
            ripples.erase(std::remove_if(ripples.begin(), ripples.end(), 
                [](const Ripple& r) { return r.life <= 0; }), ripples.end());
        }

        // 绘制部分
        // 优化：仅在可见且有活动内容时，或者刚刚发生变化时才清除和绘制
        if (appState.visible && hasActivity) {
            // 清除屏幕 (透明)
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);

            // 绘制涟漪
            for (const auto& r : ripples) {
                float alphaRatio = r.life / RIPPLE_MAX_LIFE;
                Uint8 alpha = (Uint8)(255 * alphaRatio);
                SDL_SetRenderDrawColor(renderer, r.color.r, r.color.g, r.color.b, alpha);
                
                // 使用优化后的近似圆绘制
                drawCircleApprox(renderer, r.center.x, r.center.y, r.radius);
                // 涟漪圈数略微增加 (3层)
                if (r.radius > 1.2f) drawCircleApprox(renderer, r.center.x, r.center.y, r.radius - 1.2f);
                if (r.radius > 2.4f) drawCircleApprox(renderer, r.center.x, r.center.y, r.radius - 2.4f);
            }

            // 绘制拖尾
            if (trail.size() >= 2) {
                for (size_t i = 0; i < trail.size() - 1; ++i) {
                    float t = (float)i / (float)(trail.size() - 1);
                    SDL_Color current = lerpColor(COLOR_MAIN, COLOR_AUX, t);
                    float alpha = 1.0f - t; 
                    
                    SDL_SetRenderDrawColor(renderer, current.r, current.g, current.b, (Uint8)(255 * alpha));

                    Point p1 = trail[i].pos;
                    Point p2 = trail[i+1].pos;
                    SDL_RenderLine(renderer, p1.x, p1.y, p2.x, p2.y);
                    
                    // 稍微增加拖尾粗细 (模拟20%左右的视觉增量，实际上多画一条偏移线)
                    SDL_RenderLine(renderer, p1.x + 0.5f, p1.y + 0.5f, p2.x + 0.5f, p2.y + 0.5f);
                }
            }

            SDL_RenderPresent(renderer);
        } else if (!appState.visible) {
             // 如果隐藏，偶尔清空一次并休眠长一点
             static int hideCounter = 0;
             if (hideCounter++ % 60 == 0) {
                 SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                 SDL_RenderClear(renderer);
                 SDL_RenderPresent(renderer);
             }
             SDL_Delay(100); 
             continue;
        }

        // 智能帧率控制：关闭VSync后必须手动休眠，否则会占满GPU
        if (hasActivity) {
            // 有动画时，休眠约 12ms (上限约 80 FPS)，既流畅又省电
            SDL_Delay(12);
        } else {
            // 画面静止时，降低检测频率 (约 30 FPS)，大幅降低待机消耗
            SDL_Delay(33);
        }

        // 定时内存及状态清理 (每约 600 帧/10秒 执行一次)
        // 解决长时间运行后容器容量虚高导致的潜在卡顿或内存占用问题
        static int cleanupCounter = 0;
        if (++cleanupCounter > 600) {
            // 如果 vector 预留空间包含大量未使用的内存，则缩减它
            if (ripples.capacity() > ripples.size() * 3) {
                ripples.shrink_to_fit();
            }
            // 同样整理轨迹容器
            trail.shrink_to_fit();
            
            cleanupCounter = 0;
        }
    }

    if (tray) SDL_DestroyTray(tray);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}