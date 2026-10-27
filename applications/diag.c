/**
  ******************************************************************************
  * @file    diag.c
  * @brief   启动阶段诊断实现。
  *
  * 蜂鸣器在 PB5、低电平触发（见 Core/Src/gpio.c 与 applications/buzzer.c）。
  * 这里刻意不调用 Buzzer_Beep()（它依赖 HAL_GetTick 的非阻塞状态机），而是直接
  * 翻转 GPIO + HAL_Delay，保证在任何初始化失败的现场都能立刻发出声音——只要
  * HAL_Init 之后 SysTick 正常，HAL_Delay 就可用。
  ******************************************************************************
  */
#include "diag.h"

volatile uint8_t diag_stage    = DIAG_STAGE_NONE;
volatile uint8_t diag_clock_ok = 0U;
volatile uint8_t diag_i2c_ok   = 0xFFU;
volatile uint8_t diag_can_ok   = 0xFFU;

/* PB5 / 蜂鸣器：低电平触发 */
#define DIAG_BUZZER_PORT   GPIOB
#define DIAG_BUZZER_PIN    GPIO_PIN_5

static void Diag_BuzzOnce(uint32_t on_ms, uint32_t off_ms)
{
  HAL_GPIO_WritePin(DIAG_BUZZER_PORT, DIAG_BUZZER_PIN, GPIO_PIN_RESET);  /* 低 = 响 */
  HAL_Delay(on_ms);
  HAL_GPIO_WritePin(DIAG_BUZZER_PORT, DIAG_BUZZER_PIN, GPIO_PIN_SET);    /* 高 = 停 */
  HAL_Delay(off_ms);
}

void Diag_Init(void)
{
  HAL_GPIO_WritePin(DIAG_BUZZER_PORT, DIAG_BUZZER_PIN, GPIO_PIN_SET);    /* 静音 */
  diag_stage = DIAG_STAGE_NONE;
}

void Diag_SetStage(uint8_t stage)
{
  diag_stage = stage;
}

void Diag_RecordI2c(uint8_t status)
{
  diag_i2c_ok = status;
}

void Diag_RecordCan(uint8_t status)
{
  diag_can_ok = status;
}

void Diag_Fatal(uint8_t stage)
{
  uint8_t i;

  diag_stage = stage;

  /* 报码规则：先一声长响作为"开头标记"，随后鸣叫 stage 声。
     例如 stage=7（CAN 初始化失败）就是：长响 + 7 声短响。 */
  Diag_BuzzOnce(400U, 300U);
  for (i = 0U; i < stage; i++)
  {
    Diag_BuzzOnce(120U, 220U);
  }
  HAL_Delay(500U);

  __disable_irq();
  while (1)
  {
    /* 保持静音，避免卡死时蜂鸣器一直响 */
    HAL_GPIO_WritePin(DIAG_BUZZER_PORT, DIAG_BUZZER_PIN, GPIO_PIN_SET);
  }
}
