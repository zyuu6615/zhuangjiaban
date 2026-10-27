/**
  ******************************************************************************
  * @file    diag.h
  * @brief   启动阶段诊断：蜂鸣器报错码 + 初始化结果记录。
  *
  * 目的：让板子"自己说"卡在哪一步，不依赖调试器读寄存器。
  * 进入 Error_Handler 时会用 PB5 蜂鸣器鸣叫 n 声，n 就是出错阶段号。
  ******************************************************************************
  */
#ifndef __DIAG_H
#define __DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 启动阶段编号 */
#define DIAG_STAGE_NONE     0U
#define DIAG_STAGE_HAL      1U   /* HAL_Init              */
#define DIAG_STAGE_CLOCK    2U   /* SystemClock_Config    */
#define DIAG_STAGE_GPIO     3U   /* MX_GPIO_Init          */
#define DIAG_STAGE_DMA      4U   /* MX_DMA_Init           */
#define DIAG_STAGE_TIM      5U   /* MX_TIM2_Init          */
#define DIAG_STAGE_I2C      6U   /* MX_I2C1_Init          */
#define DIAG_STAGE_CAN      7U   /* MX_CAN_Init           */
#define DIAG_STAGE_VIB      8U   /* Vibration_Init        */
#define DIAG_STAGE_OLED     9U   /* OLED_Init             */
#define DIAG_STAGE_BUZZER  10U   /* Buzzer_Init           */
#define DIAG_STAGE_LED     11U   /* WS2812 自检           */
#define DIAG_STAGE_MAIN    12U   /* 初始化通过，已进主循环 */

/* ---- 诊断信息（调试时 Watch 窗口直接看）---- */
extern volatile uint8_t diag_stage;    /* 当前/出错的阶段号 */
extern volatile uint8_t diag_clock_ok; /* 1=72MHz(HSE)  2=48MHz(HSI 兜底) */
extern volatile uint8_t diag_i2c_ok;   /* HAL_I2C_Init 返回值，0=OK */
extern volatile uint8_t diag_can_ok;   /* HAL_CAN_Init 返回值，0=OK */

/* 初始化：只需 PB5 的 GPIO 已配置（MX_GPIO_Init 之后调用） */
void Diag_Init(void);

/* 记录"当前正在执行哪个初始化阶段" */
void Diag_SetStage(uint8_t stage);

/* 记录一次非致命失败（返回码存进 diag_* 变量，程序继续跑） */
void Diag_RecordI2c(uint8_t status);
void Diag_RecordCan(uint8_t status);

/* 进入 Error_Handler 时调用：蜂鸣 n 声报出阶段号，随后死循环 */
void Diag_Fatal(uint8_t stage);

#ifdef __cplusplus
}
#endif

#endif /* __DIAG_H */
