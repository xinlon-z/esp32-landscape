#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

#include "lvgl.h"
#include "music_mqtt.h"
#include "music_player_screen.h"
#include "sim_music_mqtt.h"

namespace {

constexpr int kScreenW = 640;
constexpr int kScreenH = 172;
constexpr int kScale = 3;

struct DisplayContext {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    std::vector<lv_color_t> framebuffer;
};

struct PointerState {
    int16_t x = 0;
    int16_t y = 0;
    bool pressed = false;
};

DisplayContext s_display;
PointerState s_pointer;

void flushDisplay(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p)
{
    auto* ctx = static_cast<DisplayContext*>(disp_drv->user_data);
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    for (int32_t row = 0; row < h; ++row) {
        const int32_t dst_y = area->y1 + row;
        if (dst_y < 0 || dst_y >= kScreenH) {
            continue;
        }
        const int32_t dst_x = std::max<int32_t>(0, area->x1);
        const int32_t copy_w = std::min<int32_t>(w, kScreenW - dst_x);
        if (copy_w <= 0) {
            continue;
        }
        memcpy(&ctx->framebuffer[dst_y * kScreenW + dst_x], color_p + row * w, copy_w * sizeof(lv_color_t));
    }

    SDL_UpdateTexture(ctx->texture, nullptr, ctx->framebuffer.data(), kScreenW * sizeof(lv_color_t));
    SDL_RenderClear(ctx->renderer);
    SDL_Rect dst = {0, 0, kScreenW * kScale, kScreenH * kScale};
    SDL_RenderCopy(ctx->renderer, ctx->texture, nullptr, &dst);
    SDL_RenderPresent(ctx->renderer);
    lv_disp_flush_ready(disp_drv);
}

void readPointer(lv_indev_drv_t*, lv_indev_data_t* data)
{
    data->point.x = s_pointer.x;
    data->point.y = s_pointer.y;
    data->state = s_pointer.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

bool initDisplay()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }

    s_display.window = SDL_CreateWindow("LVGL Music UI Simulator",
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        kScreenW * kScale,
                                        kScreenH * kScale,
                                        SDL_WINDOW_SHOWN);
    s_display.renderer = SDL_CreateRenderer(s_display.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    s_display.texture = SDL_CreateTexture(s_display.renderer,
                                          SDL_PIXELFORMAT_RGB565,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          kScreenW,
                                          kScreenH);
    if (!s_display.window || !s_display.renderer || !s_display.texture) {
        std::cerr << "SDL display init failed: " << SDL_GetError() << "\n";
        return false;
    }
    s_display.framebuffer.assign(kScreenW * kScreenH, lv_color_black());

    static lv_disp_draw_buf_t draw_buf;
    static std::vector<lv_color_t> buf1(kScreenW * 48);
    static std::vector<lv_color_t> buf2(kScreenW * 48);
    lv_disp_draw_buf_init(&draw_buf, buf1.data(), buf2.data(), buf1.size());

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = kScreenW;
    disp_drv.ver_res = kScreenH;
    disp_drv.flush_cb = flushDisplay;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = &s_display;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = readPointer;
    lv_indev_drv_register(&indev_drv);
    return true;
}

void destroyDisplay()
{
    if (s_display.texture) {
        SDL_DestroyTexture(s_display.texture);
    }
    if (s_display.renderer) {
        SDL_DestroyRenderer(s_display.renderer);
    }
    if (s_display.window) {
        SDL_DestroyWindow(s_display.window);
    }
    SDL_Quit();
}

bool saveScreenshot(const char* path)
{
    if (!path || !path[0]) {
        return true;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        std::cerr << "SDL_CreateRGBSurfaceWithFormat failed: " << SDL_GetError() << "\n";
        return false;
    }

    auto* pixels = static_cast<uint32_t*>(surface->pixels);
    const int pitch_pixels = surface->pitch / static_cast<int>(sizeof(uint32_t));
    for (int y = 0; y < kScreenH; ++y) {
        for (int x = 0; x < kScreenW; ++x) {
            pixels[y * pitch_pixels + x] = 0xff000000u | lv_color_to32(s_display.framebuffer[y * kScreenW + x]);
        }
    }

    const bool ok = SDL_SaveBMP(surface, path) == 0;
    if (!ok) {
        std::cerr << "SDL_SaveBMP failed: " << SDL_GetError() << "\n";
    }
    SDL_FreeSurface(surface);
    return ok;
}

void handleEvent(const SDL_Event& event, bool* running)
{
    if (event.type == SDL_QUIT) {
        *running = false;
    } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        *running = false;
    } else if (event.type == SDL_MOUSEMOTION) {
        s_pointer.x = static_cast<int16_t>(event.motion.x / kScale);
        s_pointer.y = static_cast<int16_t>(event.motion.y / kScale);
    } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        s_pointer.x = static_cast<int16_t>(event.button.x / kScale);
        s_pointer.y = static_cast<int16_t>(event.button.y / kScale);
        s_pointer.pressed = event.type == SDL_MOUSEBUTTONDOWN;
    }
}

} // namespace

int main(int argc, char** argv)
{
    const SimMusicMqtt::Config config = SimMusicMqtt::parseArgs(argc, argv);
    SimMusicMqtt::configure(config);

    lv_init();
    if (!initDisplay()) {
        destroyDisplay();
        return 1;
    }

    MusicMqtt::init();

    MusicPlayerScreen screen;
    screen.create();

    bool running = true;
    bool recreated = false;
    const auto start = std::chrono::steady_clock::now();
    auto last_tick = std::chrono::steady_clock::now();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handleEvent(event, &running);
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick);
        if (elapsed.count() > 0) {
            lv_tick_inc(static_cast<uint32_t>(elapsed.count()));
            last_tick = now;
        }
        lv_timer_handler();
        const auto run_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (!recreated && config.recreate_at_ms > 0 && run_elapsed_ms >= config.recreate_at_ms) {
            screen.destroy();
            screen.create();
            recreated = true;
        }
        if (config.run_ms > 0 &&
            run_elapsed_ms >= config.run_ms) {
            running = false;
        }
        SDL_Delay(5);
    }

    const bool screenshot_ok = saveScreenshot(config.screenshot_path);
    screen.destroy();
    destroyDisplay();
    return screenshot_ok ? 0 : 1;
}
