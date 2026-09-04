#ifndef DRAW_UTIL_H
#define DRAW_UTIL_H

#include <SDL.h>
#include <algorithm>

// Rounded rectangles. SDL2 draws axis-aligned rects and lines and nothing else,
// so a soft corner is three rects plus four filled quarter-discs. Cheap enough
// per frame.
namespace DrawUtil {

inline void fillRound(SDL_Renderer* r, SDL_Rect q, int rad, SDL_Color c, Uint8 a = 255) {
    if (q.w <= 0 || q.h <= 0) return;
    rad = std::max(0, std::min(rad, std::min(q.w, q.h) / 2));
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
    if (rad == 0) { SDL_RenderFillRect(r, &q); return; }

    SDL_Rect mid{ q.x, q.y + rad, q.w, q.h - 2 * rad };
    SDL_RenderFillRect(r, &mid);
    SDL_Rect top{ q.x + rad, q.y, q.w - 2 * rad, rad };
    SDL_RenderFillRect(r, &top);
    SDL_Rect bot{ q.x + rad, q.y + q.h - rad, q.w - 2 * rad, rad };
    SDL_RenderFillRect(r, &bot);

    // Corners, one horizontal span per scanline.
    for (int dy = 0; dy < rad; dy++) {
        int dx = (int)(SDL_sqrtf((float)(rad * rad - (rad - dy) * (rad - dy))) + 0.5f);
        SDL_Rect l{ q.x + rad - dx, q.y + dy, dx, 1 };
        SDL_RenderFillRect(r, &l);
        SDL_Rect rr{ q.x + q.w - rad, q.y + dy, dx, 1 };
        SDL_RenderFillRect(r, &rr);
        SDL_Rect l2{ q.x + rad - dx, q.y + q.h - 1 - dy, dx, 1 };
        SDL_RenderFillRect(r, &l2);
        SDL_Rect r2{ q.x + q.w - rad, q.y + q.h - 1 - dy, dx, 1 };
        SDL_RenderFillRect(r, &r2);
    }
}

inline void frameRound(SDL_Renderer* r, SDL_Rect q, int rad, SDL_Color c, Uint8 a = 255) {
    if (q.w <= 0 || q.h <= 0) return;
    rad = std::max(0, std::min(rad, std::min(q.w, q.h) / 2));
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
    SDL_RenderDrawLine(r, q.x + rad, q.y, q.x + q.w - rad - 1, q.y);
    SDL_RenderDrawLine(r, q.x + rad, q.y + q.h - 1, q.x + q.w - rad - 1, q.y + q.h - 1);
    SDL_RenderDrawLine(r, q.x, q.y + rad, q.x, q.y + q.h - rad - 1);
    SDL_RenderDrawLine(r, q.x + q.w - 1, q.y + rad, q.x + q.w - 1, q.y + q.h - rad - 1);
    for (int i = 0; i <= 90; i += 3) {
        float t = (float)i * 3.14159265f / 180.0f;
        int dx = (int)(rad * SDL_cosf(t) + 0.5f), dy = (int)(rad * SDL_sinf(t) + 0.5f);
        SDL_RenderDrawPoint(r, q.x + rad - dx,           q.y + rad - dy);
        SDL_RenderDrawPoint(r, q.x + q.w - rad + dx - 1, q.y + rad - dy);
        SDL_RenderDrawPoint(r, q.x + rad - dx,           q.y + q.h - rad + dy - 1);
        SDL_RenderDrawPoint(r, q.x + q.w - rad + dx - 1, q.y + q.h - rad + dy - 1);
    }
}

// Soft halo behind a selected widget: concentric rounded rects, each larger and
// fainter. Fakes a blur without a shader or a render target.
inline void glowRound(SDL_Renderer* r, SDL_Rect q, int rad, SDL_Color c,
                      int spread = 10, Uint8 peak = 70) {
    for (int i = spread; i >= 1; i--) {
        SDL_Rect g{ q.x - i, q.y - i, q.w + i * 2, q.h + i * 2 };
        Uint8 a = (Uint8)(peak * (1.0f - (float)i / (float)(spread + 1)) * 0.5f);
        fillRound(r, g, rad + i, c, a);
    }
}

} // namespace DrawUtil

#endif
