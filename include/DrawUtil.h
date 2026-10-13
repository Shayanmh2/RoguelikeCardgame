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

// A slider down the side of a list or a page that runs past what fits. `pos`
// goes 0..maxPos in whatever the caller scrolls by (rows, lines), and the
// thumb is as tall as the share that shows, `view` out of `whole`.
inline SDL_Rect scrollThumb(const SDL_Rect& track, int pos, int maxPos, int view, int whole) {
    int h = whole > 0 ? (int)((long long)track.h * view / whole) : track.h;
    h = std::min(track.h, std::max(h, track.w * 3));   // never a sliver you can't grab
    const int travel = track.h - h;
    const int y = track.y + (maxPos > 0 ? travel * std::max(0, std::min(pos, maxPos)) / maxPos : 0);
    return SDL_Rect{ track.x, y, track.w, h };
}

// The pos that puts the thumb's top at `top`, for a drag or a click on the track.
inline int scrollPosAt(const SDL_Rect& track, int maxPos, int view, int whole, int top) {
    const int travel = track.h - scrollThumb(track, 0, maxPos, view, whole).h;
    if (travel <= 0 || maxPos <= 0) return 0;
    const int at = std::max(0, std::min(travel, top - track.y));
    return (at * maxPos + travel / 2) / travel;
}

inline void drawScroll(SDL_Renderer* r, const SDL_Rect& track, int pos, int maxPos, int view, int whole,
                       SDL_Color groove, SDL_Color thumb) {
    if (maxPos <= 0 || track.h <= 0) return;
    fillRound(r, track, track.w / 2, groove);
    fillRound(r, scrollThumb(track, pos, maxPos, view, whole), track.w / 2, thumb);
}

// Holding the thumb. `grab` is where on it the pointer took hold, so it
// doesn't jump to put its top under the pointer.
struct ScrollGrip { bool held = false; int grab = 0; };

// A click at (x, y). On the thumb it takes hold; anywhere else on the track
// it jumps there first, then takes hold. True if the click was the slider's.
// The bar is thin, so a click just beside it counts too.
inline bool scrollPress(ScrollGrip& g, const SDL_Rect& track, int& pos, int maxPos, int view, int whole,
                        int x, int y) {
    if (maxPos <= 0 || track.h <= 0) return false;
    if (x < track.x - track.w || x >= track.x + track.w * 2 || y < track.y || y >= track.y + track.h) return false;
    SDL_Rect t = scrollThumb(track, pos, maxPos, view, whole);
    if (y < t.y || y >= t.y + t.h) {
        pos = scrollPosAt(track, maxPos, view, whole, y - t.h / 2);
        t = scrollThumb(track, pos, maxPos, view, whole);
    }
    g.held = true;
    g.grab = y - t.y;
    return true;
}

// Every frame: a held thumb follows the pointer until the button comes up,
// even off the bar, the same as the settings sliders.
inline void scrollDrag(ScrollGrip& g, const SDL_Rect& track, int& pos, int maxPos, int view, int whole,
                       int y, bool down) {
    if (!g.held) return;
    if (!down || maxPos <= 0) { g.held = false; return; }
    pos = scrollPosAt(track, maxPos, view, whole, y - g.grab);
}

} // namespace DrawUtil

#endif
