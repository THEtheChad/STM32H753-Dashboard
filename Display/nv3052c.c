#include "nv3052c.h"
#include "gauge.h"

extern LTDC_HandleTypeDef hltdc;

/* L8 (palettized) framebuffer at a fixed address in AXI SRAM (RAM_D1, 512 KB).
 * Linker .bss lives in DTCMRAM (0x20000000) which is CPU-private — LTDC, an AXI
 * master, cannot read it. A pointer to 0x24000000 sidesteps the linker entirely.
 * 720*720*1 byte = 506.25 KB of the 512 KB bank; a full RGB565 buffer (1013 KB)
 * would not fit in any single bank, which is why the layer runs L8 + CLUT. */
#define FB_BYTES   (NV3052C_WIDTH * NV3052C_HEIGHT)
static uint8_t * const fb = (uint8_t *)0x24000000U;

/* -------------------------------------------------------------------------
 * Bit-bang 9-bit SPI
 * PA7=MOSI, PG11=SCK, PG4=CS  (Mode 3: idle HIGH, latch on rising edge)
 * -------------------------------------------------------------------------*/
#define BB_MOSI_PORT  GPIOA
#define BB_MOSI_PIN   GPIO_PIN_7
#define BB_SCK_PORT   GPIOG
#define BB_SCK_PIN    GPIO_PIN_11

/* PE14 = LTDC_CLK. Force it LOW as a GPIO during SPI init so the NV3052C
 * doesn't see a running pixel clock and ignore our SPI commands. */
#define LTDC_CLK_PORT  GPIOE
#define LTDC_CLK_PIN   GPIO_PIN_14

static void bb_spi_init(void)
{
    GPIO_InitTypeDef g = {0};
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    /* Hold LTDC pixel clock pin LOW — prevents NV3052C from ignoring SPI */
    g.Pin = LTDC_CLK_PIN;
    HAL_GPIO_Init(LTDC_CLK_PORT, &g);
    HAL_GPIO_WritePin(LTDC_CLK_PORT, LTDC_CLK_PIN, GPIO_PIN_RESET);

    g.Pin   = BB_MOSI_PIN;
    HAL_GPIO_Init(BB_MOSI_PORT, &g);
    g.Pin   = BB_SCK_PIN;
    HAL_GPIO_Init(BB_SCK_PORT, &g);

    HAL_GPIO_WritePin(BB_SCK_PORT,  BB_SCK_PIN,  GPIO_PIN_SET);   /* SCK idle HIGH */
    HAL_GPIO_WritePin(BB_MOSI_PORT, BB_MOSI_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(NV3052C_CS_GPIO_Port, NV3052C_CS_Pin, GPIO_PIN_SET);
}

static void ltdc_clk_restore(void)
{
    GPIO_InitTypeDef g = {0};
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;   /* 48 MHz PCLK needs fast slew */
    g.Alternate = GPIO_AF14_LTDC;
    g.Pin       = LTDC_CLK_PIN;
    HAL_GPIO_Init(LTDC_CLK_PORT, &g);
}

/* Clock out one 9-bit word (D/C bit + 8 data bits). CS must already be LOW. */
static void spi_word_9(uint16_t word)
{
    for (int i = 8; i >= 0; i--) {
        HAL_GPIO_WritePin(BB_SCK_PORT,  BB_SCK_PIN,  GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BB_MOSI_PORT, BB_MOSI_PIN, ((word >> i) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BB_SCK_PORT,  BB_SCK_PIN,  GPIO_PIN_SET);
    }
}

/* One command and all its parameters inside a single CS frame.
 * The previous driver raised CS after every 9-bit word; multi-parameter
 * commands — critically the 0xFF page select, which per datasheet 5.x takes
 * its three parameters (0x30, 0x52, page) as ONE command frame — may never
 * have taken effect, leaving pages 1-3 of the init unapplied. */
static void nv_write(uint8_t cmd, const uint8_t *params, size_t n)
{
    HAL_GPIO_WritePin(NV3052C_CS_GPIO_Port, NV3052C_CS_Pin, GPIO_PIN_RESET);
    spi_word_9(cmd);
    for (size_t i = 0; i < n; i++)
        spi_word_9(0x100u | params[i]);
    HAL_GPIO_WritePin(NV3052C_CS_GPIO_Port, NV3052C_CS_Pin, GPIO_PIN_SET);
}

static inline void nv_reg(uint8_t r, uint8_t v) { nv_write(r, &v, 1); }

static void nv_page(uint8_t page)
{
    const uint8_t p[3] = {0x30, 0x52, page};
    nv_write(0xFF, p, 3);
}

/* -------------------------------------------------------------------------
 * Init register table (page-switched layout)
 * -------------------------------------------------------------------------*/
typedef struct { uint8_t reg; uint8_t val; } nv_reg_t;

static const nv_reg_t nv_init[] = {
    /* Page 1: Power, VCOM, analog */
    {0xFF,0x30},{0xFF,0x52},{0xFF,0x01},
    {0xE3,0x00},{0x0A,0x11},{0x23,0xA0},{0x24,0x32},{0x25,0x12},  /* 0x23=0xA0 = manufacturer's value (DE-only). Earlier 0x80 experiment predates the CS-framing fix, when page-1 writes likely never applied. */
    {0x26,0x2E},{0x27,0x2E},{0x29,0x02},{0x2A,0xCF},{0x32,0x34},
    {0x38,0x9C},{0x39,0xA7},{0x3A,0x27},{0x3B,0x94},
    {0x42,0x6D},{0x43,0x83},{0x81,0x00},
    {0x91,0x67},{0x92,0x67},
    {0xA0,0x52},{0xA1,0x50},{0xA4,0x9C},{0xA7,0x02},{0xA8,0x02},
    {0xA9,0x02},{0xAA,0xA8},{0xAB,0x28},{0xAE,0xD2},{0xAF,0x02},
    {0xB0,0xD2},{0xB2,0x26},{0xB3,0x26},

    /* Page 2: Gamma */
    {0xFF,0x30},{0xFF,0x52},{0xFF,0x02},
    {0xB1,0x0A},{0xD1,0x0E},{0xB4,0x2F},{0xD4,0x2D},
    {0xB2,0x0C},{0xD2,0x0C},{0xB3,0x30},{0xD3,0x2A},
    {0xB6,0x1E},{0xD6,0x16},{0xB7,0x3B},{0xD7,0x35},
    {0xC1,0x08},{0xE1,0x08},{0xB8,0x0D},{0xD8,0x0D},
    {0xB9,0x05},{0xD9,0x05},{0xBD,0x15},{0xDD,0x15},
    {0xBC,0x13},{0xDC,0x13},{0xBB,0x12},{0xDB,0x10},
    {0xBA,0x11},{0xDA,0x11},{0xBE,0x17},{0xDE,0x17},
    {0xBF,0x0F},{0xDF,0x0F},{0xC0,0x16},{0xE0,0x16},
    {0xB5,0x2E},{0xD5,0x3F},{0xB0,0x03},{0xD0,0x02},

    /* Page 3: GIP timing & pin mapping */
    {0xFF,0x30},{0xFF,0x52},{0xFF,0x03},
    {0x08,0x09},{0x09,0x0A},{0x0A,0x0B},{0x0B,0x0C},
    {0x28,0x22},{0x2A,0xE9},{0x2B,0xE9},
    {0x34,0x51},{0x35,0x01},{0x36,0x26},{0x37,0x13},
    {0x40,0x07},{0x41,0x08},{0x42,0x09},{0x43,0x0A},
    {0x44,0x22},{0x45,0xDB},{0x46,0xDC},{0x47,0x22},{0x48,0xDD},{0x49,0xDE},
    {0x50,0x0B},{0x51,0x0C},{0x52,0x0D},{0x53,0x0E},
    {0x54,0x22},{0x55,0xDF},{0x56,0xE0},{0x57,0x22},{0x58,0xE1},{0x59,0xE2},
    {0x80,0x1E},{0x81,0x1E},{0x82,0x1F},{0x83,0x1F},
    {0x84,0x05},{0x85,0x0A},{0x86,0x0A},{0x87,0x0C},{0x88,0x0C},
    {0x89,0x0E},{0x8A,0x0E},{0x8B,0x10},{0x8C,0x10},
    {0x8D,0x00},{0x8E,0x00},{0x8F,0x1F},{0x90,0x1F},
    {0x91,0x1E},{0x92,0x1E},{0x93,0x02},{0x94,0x04},
    {0x96,0x1E},{0x97,0x1E},{0x98,0x1F},{0x99,0x1F},
    {0x9A,0x05},{0x9B,0x09},{0x9C,0x09},{0x9D,0x0B},{0x9E,0x0B},
    {0x9F,0x0D},{0xA0,0x0D},{0xA1,0x0F},{0xA2,0x0F},
    {0xA3,0x00},{0xA4,0x00},{0xA5,0x1F},{0xA6,0x1F},
    {0xA7,0x1E},{0xA8,0x1E},{0xA9,0x01},{0xAA,0x03},

    /* Page 0: Display control */
    {0xFF,0x30},{0xFF,0x52},{0xFF,0x00},

    /* MADCTL — bit assignments per NV3052CGRB datasheet 5.2.21:
     *   D3 = bgr  (0 = RGB, 1 = BGR color filter order)
     *   D1 = ss   (0 = source scan L→R, 1 = source scan R→L)
     *   D0 = gs   (0 = gate scan T→B,  1 = gate scan B→T)
     *
     * Historical half-split evidence (pre CS-framing fix): the garbled half
     * followed the ss bit (0x0A → left garbled, 0x00 → right garbled), i.e.
     * whichever bank latches the LATE pixels of each line. Video analysis
     * showed the garbled half never latches ANY data (stripes frozen across
     * BCCR color changes) — consistent with the panel running on power-on
     * defaults because the page 1-3 init never applied (see nv_write). */
    {0x36,0x0A},  /* MADCTL — manufacturer's value */
};

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void NV3052C_Init(void)
{
    /* Stop LTDC — NV3052C ignores SPI while PCLK is running */
    __HAL_LTDC_DISABLE(&hltdc);
    HAL_Delay(20);

    bb_spi_init();

    /* Hardware reset */
    HAL_GPIO_WritePin(NV3052C_RST_GPIO_Port, NV3052C_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(NV3052C_RST_GPIO_Port, NV3052C_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(15);
    HAL_GPIO_WritePin(NV3052C_RST_GPIO_Port, NV3052C_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(150);

    /* Send init sequence. The table keeps the manufacturer's layout where a
     * page switch appears as three consecutive 0xFF entries (0x30, 0x52, page);
     * those are collapsed into one 3-parameter command frame here. */
    for (size_t i = 0; i < sizeof(nv_init) / sizeof(nv_reg_t); i++) {
        if (nv_init[i].reg == 0xFF) {
            nv_page(nv_init[i + 2].val);
            i += 2;
        } else {
            nv_reg(nv_init[i].reg, nv_init[i].val);
        }
    }

    nv_reg(0x11, 0x00);   /* Sleep Out */
    HAL_Delay(200);
    nv_reg(0x29, 0x00);   /* Display On */
    HAL_Delay(100);

    /* Render the static gauge face, then hand the framebuffer to the LTDC. */
    Gauge_RenderSpeedo(fb, NV3052C_WIDTH, NV3052C_HEIGHT, 55.0f);
    SCB_CleanDCache_by_Addr((uint32_t *)fb, FB_BYTES);

    ltdc_clk_restore();
    __HAL_LTDC_ENABLE(&hltdc);
    hltdc.Instance->BCCR = 0x00000000U;

    /* Layer 0: L8 + CLUT, full screen, opaque. Configured here rather than in
     * the CubeMX-generated MX_LTDC_Init so an .ioc regeneration can't stomp it
     * (MX_LTDC_Init's placeholder layer config is simply overwritten). */
    {
        LTDC_LayerCfgTypeDef cfg = {0};
        cfg.WindowX0 = 0; cfg.WindowX1 = NV3052C_WIDTH;
        cfg.WindowY0 = 0; cfg.WindowY1 = NV3052C_HEIGHT;
        cfg.PixelFormat = LTDC_PIXEL_FORMAT_L8;
        cfg.Alpha = 255;
        cfg.Alpha0 = 0;
        cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
        cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
        cfg.FBStartAdress = (uint32_t)fb;
        cfg.ImageWidth  = NV3052C_WIDTH;
        cfg.ImageHeight = NV3052C_HEIGHT;
        HAL_LTDC_ConfigCLUT(&hltdc, (uint32_t *)Gauge_CLUT, 256, 0);
        HAL_LTDC_EnableCLUT(&hltdc, 0);
        HAL_LTDC_ConfigLayer(&hltdc, &cfg, 0);
    }
}

/* Static image for now — real-time needle updates come with sensor data. */
void NV3052C_Update(void)
{
}
