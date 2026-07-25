#ifndef GAUGE_H
#define GAUGE_H

#include <stdint.h>

/* 256-entry CLUT for the L8 framebuffer (0x00RRGGBB, LTDC CLUTWR layout) */
extern const uint32_t Gauge_CLUT[256];

/* One-shot render: face + needle (used by the host-side preview). */
void Gauge_RenderSpeedo(uint8_t *fb, int w, int h, float mph);

/* Split API for animation: render the static face once, then move the
 * needle as often as desired. Gauge_SetNeedle restores the pixels under
 * the previous needle from an undo log, draws the new one, and reports
 * the dirty byte range within the framebuffer (for targeted cache
 * cleaning); pass NULL if unwanted. */
void Gauge_RenderFace(uint8_t *fb, int w, int h);
void Gauge_SetNeedle(float mph, uint32_t *dirty_first, uint32_t *dirty_last);

#endif /* GAUGE_H */
