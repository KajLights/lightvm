#ifndef LIGHTVM_DRAW_H
#define LIGHTVM_DRAW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DRAW_RGB = 3, DRAW_RGBA = 12, DRAW_SPRITES = 13,
       DRAW_IMAGE_RGB = 14, DRAW_IMAGE_RGBA, DRAW_IMAGE_RGBW, DRAW_IMAGE_RGBWA,
       DRAW_BATCH, DRAW_STYLE };
typedef struct { float translate, scale, origin, alpha, brightness; uint32_t tint; float hue, saturation; } DrawStyle;
/* Generic instance storage. No placement, lifetime, fade or expiry policy. */
typedef struct { uint64_t born, previous; uint32_t active; } DrawInstance;
typedef struct { uint32_t active, reserved; } DrawBatch;
size_t draw_batch_bytes(unsigned capacity, unsigned fields);
DrawInstance *draw_instance(DrawBatch *, unsigned slot, unsigned fields);
uint32_t *draw_instance_fields(DrawInstance *);
unsigned draw_channels(unsigned format);
int draw_channel(unsigned format, unsigned channel);
/* Schema 6 compatibility only. New SpriteBatch declarations use DrawBatch. */
typedef struct { uint32_t position, color; float age, lifetime; uint32_t active; } DrawSprite;
typedef struct { uint32_t free_count, active_count; } DrawSprites;
unsigned draw_stride(unsigned format);
uint8_t draw_mul255(unsigned value, unsigned alpha);
void draw_rgba(uint8_t *out, uint32_t color, unsigned alpha);
void draw_over(uint8_t *dst, const uint8_t *src, unsigned opacity);
uint32_t draw_colorwheel(float hue);
size_t draw_sprites_bytes(unsigned capacity, unsigned pixels);
uint16_t *draw_free_positions(DrawSprites *batch);
DrawSprite *draw_sprite_records(DrawSprites *batch, unsigned pixels);
void draw_sprites_init(DrawSprites *batch, unsigned capacity, unsigned pixels);
bool draw_sprite_spawn(DrawSprites *, DrawSprite *, unsigned slot, unsigned rank, uint32_t color, float lifetime);
void draw_sprite_advance(DrawSprites *, DrawSprite *, unsigned slot, float delta);
void draw_sprite_paint(uint8_t *rgba, const DrawSprite *sprite);
#endif
