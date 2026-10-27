/**
  ******************************************************************************
  * @file    ws2812.h
  * @brief   WS2812 灯带驱动（PA0 / TIM2_CH1 + DMA），多色封装接口。
  ******************************************************************************
  */
#ifndef __WS2812_H
#define __WS2812_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 级联 LED 总数（两级 × 8 颗） */
#define WS2812_NUM   16

/* 颜色类型：分量 0~255 */
typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} WS2812_Color_t;

/* 预定义颜色（可直接作为函数实参使用） */
#define WS2812_RED      ((WS2812_Color_t){255,   0,   0})
#define WS2812_GREEN    ((WS2812_Color_t){  0, 255,   0})
#define WS2812_BLUE     ((WS2812_Color_t){  0,   0, 255})
#define WS2812_YELLOW   ((WS2812_Color_t){255, 255,   0})
#define WS2812_CYAN     ((WS2812_Color_t){  0, 255, 255})
#define WS2812_MAGENTA  ((WS2812_Color_t){255,   0, 255})
#define WS2812_WHITE    ((WS2812_Color_t){255, 255, 255})
#define WS2812_BLACK    ((WS2812_Color_t){  0,   0,   0})

/**
  * @brief  按实际 TIM2 时钟重算时序参数并配置 ARR/CCR1。
  *         必须在 MX_TIM2_Init() 之后、第一次 WS2812_Send() 之前调用。
  */
void WS2812_Init(void);

/**
  * @brief  设置单颗 LED 的颜色（只写缓冲区，不发送）。
  * @param  index: LED 序号 0 ~ WS2812_NUM-1。
  * @param  color: 目标颜色。
  */
void WS2812_SetPixel(uint32_t index, WS2812_Color_t color);

/**
  * @brief  以独立 RGB 分量设置单颗 LED（不经过结构体传参）。
  */
void WS2812_SetPixelRGB(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

/**
  * @brief  把所有 LED 设为同一颜色并按百分比缩放亮度（只写缓冲区，不发送）。
  * @param  r,g,b:   目标颜色分量（按满亮度给出）。
  * @param  percent: 亮度百分比 0~100；0 等于熄灭。击打闪烁用它与常亮态拉开亮度差。
  */
void WS2812_Show(uint8_t r, uint8_t g, uint8_t b, uint8_t percent);

/**
  * @brief  把所有 LED 设为同一颜色（只写缓冲区，不发送）。
  */
void WS2812_SetAll(WS2812_Color_t color);

/**
  * @brief  以 RGB 分量把所有 LED 设为同一颜色（只写缓冲区，不发送）。
  */
void WS2812_SetAllRGB(uint8_t r, uint8_t g, uint8_t b);

/**
  * @brief  熄灭所有 LED（只写缓冲区，需再调用 WS2812_Send 生效）。
  */
void WS2812_Clear(void);

/**
  * @brief  通过 PWM+DMA 把当前缓冲区刷新到灯带。
  */
void WS2812_Send(void);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812_H */
