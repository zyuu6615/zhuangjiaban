/**
  ******************************************************************************
  * @file    ws2812.c
  * @brief   WS2812 灯带驱动实现。
  *
  * 原理：WS2812 单总线归零码，每 1.25µs 发 1bit，靠高电平脉宽区分 0/1。
  * 每个 bit 映射成一个 PWM 周期的比较值(CCR)，用 DMA 把整段 CCR 数组逐周期
  * 搬入 TIM2->CCR1（PA0 / TIM2_CH1），由硬件精确产生波形。
  *
  * 时序（TIM2 时钟 72MHz，ARR=89 → 90 计数 = 1.25µs）：
  *   - 0 码高电平 = WS2812_CODE0 计数 ≈ 0.42µs
  *   - 1 码高电平 = WS2812_CODE1 计数 ≈ 0.83µs
  *   - 帧尾 WS2812_RESET 个 0 周期 ≈ 60µs，完成复位/锁存并使 PA0 收尾保持低电平
  ******************************************************************************
  */
#include "ws2812.h"
#include "tim.h"

#define WS2812_RESET   48                      /* 帧尾复位/锁存周期数（≈60µs） */
#define WS2812_BITS    (WS2812_NUM * 24)       /* 每颗 24bit (GRB) */
#define WS2812_BUFLEN  (WS2812_BITS + WS2812_RESET)

/* 时序参数按实际 TIM2 时钟现算，不再写死按 72MHz 推导的常数。
   原因：SystemClock_Config 在 HSE 失效时会兜底切到 HSI（主频可能变成 48MHz），
   若仍按 72MHz 的 ARR/CCR 发码，位周期会被拉长 1.5 倍，灯带直接不认。
   计算全部向上取整并钳位，保证 0 码 < 1 码 < 周期。 */
#define WS2812_BIT_NS      1250U   /* 位周期 1.25µs */
#define WS2812_CODE0_NS     420U   /* 0 码高电平 ≈0.42µs（规格 0.4µs ±150ns） */
#define WS2812_CODE1_NS     830U   /* 1 码高电平 ≈0.83µs（规格 0.8µs ±150ns） */

static uint16_t ws2812_arr   = 89U;
static uint16_t ws2812_code0 = 30U;
static uint16_t ws2812_code1 = 60U;

/* DMA 逐周期搬入 TIM2->CCR1 的比较值缓冲区；半字以匹配 DMA HALFWORD 对齐。
   静态变量零初始化，帧尾复位段始终保持 0，无需显式填充。 */
static uint16_t ws2812_buf[WS2812_BUFLEN];

/* 根据实际 TIM2 输入时钟重算 ARR 与两个码值的计数值 */
static uint16_t Ws2812_Counts(uint32_t ns, uint32_t timer_hz, uint16_t max_period)
{
  uint64_t counts = ((uint64_t)timer_hz * (uint64_t)ns + 999999999ULL) / 1000000000ULL;

  if (counts == 0ULL)
  {
    counts = 1ULL;
  }
  if (counts > (uint64_t)max_period)
  {
    counts = (uint64_t)max_period;
  }
  return (uint16_t)counts;
}

void WS2812_Init(void)
{
  uint32_t timer_hz = HAL_RCC_GetPCLK1Freq();

  /* APB1 预分频不为 1 时，定时器时钟是 PCLK1 的 2 倍 */
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
  {
    timer_hz *= 2U;
  }

  ws2812_arr = (uint16_t)(Ws2812_Counts(WS2812_BIT_NS, timer_hz, 0xFFFFU) - 1U);
  if (ws2812_arr < 4U)
  {
    ws2812_arr = 4U;
  }

  ws2812_code0 = Ws2812_Counts(WS2812_CODE0_NS, timer_hz, ws2812_arr);
  ws2812_code1 = Ws2812_Counts(WS2812_CODE1_NS, timer_hz, ws2812_arr);

  if (ws2812_code1 <= ws2812_code0)
  {
    ws2812_code1 = (uint16_t)(ws2812_code0 + 1U);
  }

  /* 写回定时器和通道配置：ARR 决定位周期，CCR1 初值 0 保证静止时输出低电平 */
  __HAL_TIM_SET_AUTORELOAD(&htim2, ws2812_arr);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
}

/* 标量版本：直接给 r/g/b 分量，不经过结构体传参，最不易出错 */
void WS2812_SetPixelRGB(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
  if (index >= WS2812_NUM)
  {
    return;
  }

  /* WS2812 线序为 GRB，每字节 MSB 先发 */
  uint8_t grb[3] = { g, r, b };
  uint32_t idx = index * 24u;

  for (uint32_t c = 0; c < 3; c++)
  {
    for (int8_t bit = 7; bit >= 0; bit--)
    {
      ws2812_buf[idx++] = (grb[c] & (1u << bit)) ? ws2812_code1 : ws2812_code0;
    }
  }
}

void WS2812_SetPixel(uint32_t index, WS2812_Color_t color)
{
  WS2812_SetPixelRGB(index, color.r, color.g, color.b);
}

/* 按百分比缩放分量：WS2812 各分量亮度近似线性。
   用 (v * percent + 50) / 100 做四舍五入，避免小百分比被整数除法截断成全灭。
   这里刻意用独立的 r/g/b 标量而不是结构体传参，避免结构体传参在 -O0 下的
   寄存器/栈交接问题。 */
static uint8_t ScaleChan(uint8_t v, uint8_t percent)
{
  return (uint8_t)(((uint32_t)v * (uint32_t)percent + 50U) / 100U);
}

/* 直接按 RGB 分量 + 亮度百分比设置整条灯带，绕过任何结构体传参 */
void WS2812_Show(uint8_t r, uint8_t g, uint8_t b, uint8_t percent)
{
  if (percent > 100U)
  {
    percent = 100U;
  }

  uint8_t sr = ScaleChan(r, percent);
  uint8_t sg = ScaleChan(g, percent);
  uint8_t sb = ScaleChan(b, percent);

  for (uint32_t i = 0; i < WS2812_NUM; i++)
  {
    WS2812_SetPixelRGB(i, sr, sg, sb);
  }
}

void WS2812_SetAll(WS2812_Color_t color)
{
  for (uint32_t i = 0; i < WS2812_NUM; i++)
  {
    WS2812_SetPixel(i, color);
  }
}

void WS2812_SetAllRGB(uint8_t r, uint8_t g, uint8_t b)
{
  WS2812_Color_t color = { r, g, b };
  WS2812_SetAll(color);
}

void WS2812_Clear(void)
{
  WS2812_SetAll(WS2812_BLACK);
}

void WS2812_Send(void)
{
  /* Normal 模式下传输完成时 HAL 会在 DMA1_Channel5_IRQHandler 中自动关闭 CC1
     的 DMA 请求并把通道状态置回 READY，因此每次刷新可直接再次调用本函数。 */
  HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_1, (uint32_t *)ws2812_buf, WS2812_BUFLEN);
}
