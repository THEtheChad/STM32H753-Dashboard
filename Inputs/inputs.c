#include "inputs.h"
#include "config.h"

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;

InputData inputs = {0};

/* -------------------------------------------------------------------------
 * VSS — TIM2 CH1 input capture on PA0
 *
 * TIM2 is 32-bit, clocked at 64 MHz (HSI direct, APB1 timer clock).
 * At 64 MHz tick rate, 32-bit period covers ~67 seconds — far longer than
 * any realistic VSS pulse interval. We can use the raw capture delta as
 * the pulse period without overflow handling.
 *
 * Smoothing: simple moving average over the last N periods, where N is
 * calibration.vssPulsesToAverage (user-tunable via app, capped at
 * VSS_MAX_AVG in code). Holley convention is "at least 1/4 of your tooth
 * count" — 5 is a reasonable default for a 17-tooth T56.
 *
 * Pulse → MPH:
 *   avg_period = mean of last N captured periods
 *   freq_Hz    = VSS_TIM_CLK_HZ / avg_period
 *   mph        = freq_Hz × 3600 / pulses_per_mile
 *              = (VSS_TIM_CLK_HZ × 3600) / (avg_period × pulses_per_mile)
 * -------------------------------------------------------------------------*/
/* TIM2 kernel clock, derived at init from the live RCC tree (2x APB1
 * while the APB divider > 1) so clock-tree changes can't skew speed */
static uint32_t vssTimClkHz;
#define VSS_TIMEOUT_MS          2000U       /* no pulse for 2s → 0 mph         */
#define VSS_MIN_PERIOD_TICKS    100U        /* < 100 ticks (~1.5 µs) is noise  */
#define VSS_MAX_AVG             16          /* hard cap on smoothing window    */

static volatile uint32_t vssLastCaptureTick = 0;
static volatile uint32_t vssLastPulseMs     = 0;
static volatile uint8_t  vssHaveFirstCap    = 0;

/* Circular buffer of recent period samples (in timer ticks).
 * Writer: HAL_TIM_IC_CaptureCallback (ISR). Readers: Inputs_Update.
 * 32-bit writes are atomic on Cortex-M7; index/count fit in a byte. */
static volatile uint32_t vssPeriodBuf[VSS_MAX_AVG] = {0};
static volatile uint8_t  vssBufIdx   = 0;   /* next write position */
static volatile uint8_t  vssBufCount = 0;   /* valid entries, capped at VSS_MAX_AVG */

/* -------------------------------------------------------------------------
 * Tachometer — TIM1 CH1 input capture on PE9
 *
 * TIM1 is 16-bit. APB2 timer clock = 64 MHz. We set the prescaler to 999
 * (divider 1000) here so the timer ticks at 64 kHz — 65,536 ticks covers
 * ~1024 ms, which is comfortably longer than any realistic pulse interval
 * from cranking RPM (~200 rpm) to redline (~8000 rpm) on any signal source
 * (coil-negative single-pulse, distributor pickup, ECU tach output).
 *
 * Pulse → RPM:
 *   freq_Hz = TACH_TIM_CLK_HZ / period_ticks
 *   rpm     = freq_Hz × 60 / pulses_per_rev
 * -------------------------------------------------------------------------*/
#define TACH_TIM_CLK_HZ         64000U      /* target tick rate; prescaler derived at init */
#define TACH_TIMEOUT_MS         2000U       /* no pulse → 0 rpm */
#define TACH_MIN_PERIOD_TICKS   5U          /* ignore impossibly fast pulses */
#define TACH_EMA_ALPHA          0.35f       /* snappier than VSS — tach should feel live */

static volatile uint16_t tachLastCaptureTick = 0;
static volatile uint16_t tachLastPeriodTicks = 0;
static volatile uint32_t tachLastPulseMs     = 0;
static volatile uint8_t  tachHaveFirstCap    = 0;

/* -------------------------------------------------------------------------
 * Bench VSS test signal — set to 0 (or cut the PC9 jumper) for the vehicle.
 *
 * Generates pulses on PC9 (TIM3_CH4, morpho CN12 pin 1). Jumper PC9 to the
 * VSS input PA0 (Zio CN10 pin 29) and the needle shows speed measured by
 * the REAL capture -> smoothing -> source-selection pipeline. The profile
 * sweeps 0 -> 100 -> 0 MPH over 24 s so motion is obvious.
 * -------------------------------------------------------------------------*/
#define VSS_TEST_SIGNAL 1

#if VSS_TEST_SIGNAL
static TIM_HandleTypeDef htim3Test;
#define TEST_TICK_HZ 10000U

static void vss_test_signal_init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_9;                 /* PC9 = TIM3_CH4, AF2 */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOC, &g);

    htim3Test.Instance               = TIM3;
    htim3Test.Init.Prescaler         = (2U * HAL_RCC_GetPCLK1Freq()) / TEST_TICK_HZ - 1U;
    htim3Test.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3Test.Init.Period            = TEST_TICK_HZ / 10U;    /* placeholder until first update */
    htim3Test.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3Test.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3Test);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = htim3Test.Init.Period / 2U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&htim3Test, &oc, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim3Test, TIM_CHANNEL_4);
}

static void vss_test_signal_update(uint32_t nowMs)
{
    static uint32_t lastMs = 0;
    if (nowMs - lastMs < 100U) return;        /* retune 10x per second */
    lastMs = nowMs;

    float phase = (float)(nowMs % 24000U) / 1000.0f;      /* 0..24 s   */
    float mph   = (phase < 12.0f ? phase : 24.0f - phase) * (100.0f / 12.0f);
    float hz    = mph * (float)calibration.vssPulsesPerMile / 3600.0f;

    if (hz < 1.0f) {                          /* "stopped": no pulses  */
        __HAL_TIM_SET_COMPARE(&htim3Test, TIM_CHANNEL_4, 0);
        return;
    }
    uint32_t arr = (uint32_t)((float)TEST_TICK_HZ / hz);
    if (arr < 2U) arr = 2U;
    if (arr > 65535U) arr = 65535U;
    __HAL_TIM_SET_AUTORELOAD(&htim3Test, arr - 1U);
    __HAL_TIM_SET_COMPARE(&htim3Test, TIM_CHANNEL_4, arr / 2U);
}
#endif /* VSS_TEST_SIGNAL */

void Inputs_Init(void)
{
    /* TIM2 (VSS) — interrupt-driven input capture. */
    vssTimClkHz = 2U * HAL_RCC_GetPCLK1Freq();
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);

    /* TIM1 (tach) — derive the prescaler for a 64 kHz tick from the live
     * clock so the 16-bit counter always covers cranking-RPM periods.
     * CubeMX still generates Prescaler=0; this overrides it. */
    __HAL_TIM_SET_PRESCALER(&htim1, (2U * HAL_RCC_GetPCLK2Freq()) / TACH_TIM_CLK_HZ - 1U);

#if VSS_TEST_SIGNAL
    vss_test_signal_init();
#endif
    HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_1);

    /* CubeMX didn't enable NVIC for either timer (input-capture-only timers
     * without the "global interrupt" checkbox set in the .ioc). Enable both
     * manually so HAL_TIM_IC_CaptureCallback actually fires.
     * TODO: move into CubeMX so it survives regeneration. */
    HAL_NVIC_SetPriority(TIM2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_NVIC_SetPriority(TIM1_CC_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM1_CC_IRQn);
}

void Inputs_Update(void)
{
    uint32_t now = HAL_GetTick();

#if VSS_TEST_SIGNAL
    vss_test_signal_update(now);
#endif

    /* --- VSS: average of last N pulse periods → MPH ------------------- */
    {
        /* Clamp the configured window to what we actually have stored. */
        uint8_t N = calibration.vssPulsesToAverage;
        if (N > VSS_MAX_AVG) N = VSS_MAX_AVG;

        /* Atomic snapshot of the buffer state. The buffer entries themselves
         * are 32-bit (atomic on Cortex-M7), so we only need the lock long
         * enough to grab idx/count/timestamp consistently. */
        uint8_t  bufIdx, bufCount;
        uint32_t lastPulseMs;
        __disable_irq();
        bufIdx      = vssBufIdx;
        bufCount    = vssBufCount;
        lastPulseMs = vssLastPulseMs;
        __enable_irq();

        if (N > bufCount) N = bufCount;
        uint32_t ppm = calibration.vssPulsesPerMile;

        if (N == 0 || ppm == 0 || (now - lastPulseMs) > VSS_TIMEOUT_MS) {
            inputs.speedMph = 0.0f;
        } else {
            /* Sum the most recent N entries, walking backwards from the
             * next-write slot. Each buffer slot is a single uint32 written
             * atomically by the ISR, so no further locking needed. */
            uint64_t sum = 0;
            uint8_t  idx = (uint8_t)((bufIdx + VSS_MAX_AVG - 1) % VSS_MAX_AVG);
            for (uint8_t i = 0; i < N; i++) {
                sum += vssPeriodBuf[idx];
                idx = (uint8_t)((idx + VSS_MAX_AVG - 1) % VSS_MAX_AVG);
            }
            uint32_t avgPeriod = (uint32_t)(sum / N);
            inputs.speedMph = ((float)vssTimClkHz * 3600.0f) /
                              ((float)avgPeriod * (float)ppm);
        }
    }

    /* --- Tach: latest captured period → RPM --------------------------- */
    {
        uint16_t periodTicks;
        uint32_t lastPulseMs;
        __disable_irq();
        periodTicks = tachLastPeriodTicks;
        lastPulseMs = tachLastPulseMs;
        __enable_irq();

        uint8_t ppr = calibration.tachPulsesPerRev;
        if (periodTicks == 0 || ppr == 0 || (now - lastPulseMs) > TACH_TIMEOUT_MS) {
            inputs.rpm = 0.0f;
        } else {
            /* freq_Hz × 60 / ppr; combined: (64000 × 60) / (period × ppr) */
            float instantRpm = ((float)TACH_TIM_CLK_HZ * 60.0f) /
                               ((float)periodTicks * (float)ppr);
            inputs.rpm = TACH_EMA_ALPHA * instantRpm +
                         (1.0f - TACH_EMA_ALPHA) * inputs.rpm;
        }
    }

    /* --- GPIO inputs -------------------------------------------------- */
    inputs.leftTurn  = HAL_GPIO_ReadPin(TURN_LEFT_IN_GPIO_Port,  TURN_LEFT_IN_Pin)  == GPIO_PIN_SET;
    inputs.rightTurn = HAL_GPIO_ReadPin(TURN_RIGHT_IN_GPIO_Port, TURN_RIGHT_IN_Pin) == GPIO_PIN_SET;
    inputs.highBeam  = HAL_GPIO_ReadPin(HIGHBEAM_IN_GPIO_Port,   HIGHBEAM_IN_Pin)   == GPIO_PIN_SET;
}

/* HAL invokes this on every captured edge across any timer/channel.
 * Filter on instance + channel to identify which input fired. */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        uint32_t now = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        if (vssHaveFirstCap) {
            uint32_t period = now - vssLastCaptureTick;  /* 32-bit wraparound natural */
            if (period >= VSS_MIN_PERIOD_TICKS) {
                /* Push into ring buffer. Reader (Inputs_Update) handles the
                 * "how many entries to actually consume" decision. */
                vssPeriodBuf[vssBufIdx] = period;
                vssBufIdx = (uint8_t)((vssBufIdx + 1) % VSS_MAX_AVG);
                if (vssBufCount < VSS_MAX_AVG) vssBufCount++;
                vssLastPulseMs = HAL_GetTick();
            }
        } else {
            vssHaveFirstCap = 1;
        }
        vssLastCaptureTick = now;
        return;
    }

    if (htim->Instance == TIM1 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        uint16_t now = (uint16_t)HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        if (tachHaveFirstCap) {
            uint16_t period = now - tachLastCaptureTick;  /* 16-bit wraparound natural */
            if (period >= TACH_MIN_PERIOD_TICKS) {
                tachLastPeriodTicks = period;
                tachLastPulseMs     = HAL_GetTick();
            }
        } else {
            tachHaveFirstCap = 1;
        }
        tachLastCaptureTick = now;
        return;
    }
}

/* TIM2 global IRQ — routes to HAL for VSS capture. */
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

/* TIM1 capture/compare IRQ — routes to HAL for tach capture.
 * Note: TIM1 splits its interrupts across multiple vectors
 * (UP/CC/BRK/TRG). Capture events fire on TIM1_CC_IRQn. */
void TIM1_CC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim1);
}
