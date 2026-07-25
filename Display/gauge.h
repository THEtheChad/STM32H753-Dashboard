#ifndef GAUGE_H
#define GAUGE_H

#include <stdint.h>

/* 256-entry CLUT for the L8 framebuffer (0x00RRGGBB, LTDC CLUTWR layout) */
extern const uint32_t Gauge_CLUT[256];

/* Render the retro speedometer face + needle into an L8 framebuffer.
 * mph selects the (static, for now) needle position. */
void Gauge_RenderSpeedo(uint8_t *fb, int w, int h, float mph);

#endif /* GAUGE_H */
