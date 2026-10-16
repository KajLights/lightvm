#include "draw.h"
#include <math.h>
#include <string.h>

unsigned draw_stride(unsigned format) { return format == DRAW_RGBA ? 4u : 3u; }
uint8_t draw_mul255(unsigned value, unsigned alpha) { return (uint8_t)((value * alpha + 127u) / 255u); }
void draw_rgba(uint8_t *out, uint32_t color, unsigned alpha) {
    out[0] = draw_mul255(color >> 16, alpha);
    out[1] = draw_mul255((color >> 8) & 255u, alpha);
    out[2] = draw_mul255(color & 255u, alpha);
    out[3] = (uint8_t)alpha;
}
void draw_over(uint8_t *dst, const uint8_t *src, unsigned opacity) {
    unsigned alpha = draw_mul255(src[3], opacity);
    for (unsigned c = 0; c < 4; ++c)
        dst[c] = (uint8_t)(draw_mul255(src[c], opacity) + draw_mul255(dst[c], 255u - alpha));
}
uint32_t draw_colorwheel(float hue) {
    hue = fmodf(hue, 256.0f);
    if (hue < 0) hue += 256.0f;
    unsigned sector = hue < 85 ? 0 : hue < 170 ? 1 : 2;
    unsigned rising = (unsigned)((hue - (float)(sector * 85)) * 3);
    if (rising > 255) rising = 255;
    unsigned falling = 255 - rising;
    return sector == 0 ? (falling << 16 | rising << 8) : sector == 1 ? (falling << 8 | rising) : (rising << 16 | falling);
}
static size_t records_offset(unsigned pixels) {
    size_t size = sizeof(DrawSprites) + (size_t)pixels * sizeof(uint16_t);
    return (size + _Alignof(DrawSprite) - 1) & ~(size_t)(_Alignof(DrawSprite) - 1);
}
size_t draw_sprites_bytes(unsigned capacity, unsigned pixels) {
    return records_offset(pixels) + (size_t)capacity * sizeof(DrawSprite);
}
uint16_t *draw_free_positions(DrawSprites *batch) { return (uint16_t *)(batch + 1); }
DrawSprite *draw_sprite_records(DrawSprites *batch, unsigned pixels) {
    return (DrawSprite *)((uint8_t *)batch + records_offset(pixels));
}
void draw_sprites_init(DrawSprites *batch, unsigned capacity, unsigned pixels) {
    memset(batch, 0, draw_sprites_bytes(capacity, pixels));
    batch->free_count = pixels;
    for (unsigned i = 0; i < pixels; ++i) draw_free_positions(batch)[i] = (uint16_t)i;
}
bool draw_sprite_spawn(DrawSprites *batch, DrawSprite *sprites, unsigned slot, unsigned rank, uint32_t color, float lifetime) {
    if (sprites[slot].active || rank >= batch->free_count) return false;
    uint16_t *free_positions = draw_free_positions(batch);
    sprites[slot] = (DrawSprite){.position=free_positions[rank], .color=color, .lifetime=lifetime, .active=1};
    free_positions[rank] = free_positions[--batch->free_count];
    ++batch->active_count;
    return true;
}
void draw_sprite_advance(DrawSprites *batch, DrawSprite *sprites, unsigned slot, float delta) {
    DrawSprite *s = &sprites[slot];
    if (!s->active) return;
    s->age += delta;
    if (s->age > s->lifetime) {
        draw_free_positions(batch)[batch->free_count++] = (uint16_t)s->position;
        s->active = 0; --batch->active_count;
    }
}
void draw_sprite_paint(uint8_t *rgba, const DrawSprite *sprite) {
    if (!sprite->active) return;
    float progress = fminf(1.0f, sprite->age / sprite->lifetime);
    float fade = 1.0f - sinf(progress * 1.5707963267948966f);
    unsigned alpha = (unsigned)(fmaxf(0, fade) * 255.0f + 0.5f);
    uint8_t pixel[4]; draw_rgba(pixel, sprite->color, alpha);
    draw_over(rgba + (size_t)sprite->position * 4, pixel, 255);
}
