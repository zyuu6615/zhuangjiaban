#include "buzzer.h"
#include "main.h"

/* 蜂鸣器低电平触发，响一次的持续时间；刻意短于振动去抖窗口(50ms)，
   保证连续振动时蜂鸣器每次都能先关闭再重新拉低，听起来是一连串短促的
   "滴滴"声，而不是连成一片的长音 */
#define BUZZER_BEEP_MS   30U

static volatile uint8_t buzzer_active = 0;
static volatile uint32_t buzzer_start_tick = 0;

void Buzzer_Init(void)
{
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);   /* 拉高 = 不触发 */
  buzzer_active = 0;
}

/* 非阻塞触发一次蜂鸣：拉低开始，具体时长到点由 Buzzer_Update() 拉高关闭。
   若上一声还没结束，本次触发忽略，避免打断/延长导致连成长音 */
void Buzzer_Beep(void)
{
  if (buzzer_active)
  {
    return;
  }
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
  buzzer_start_tick = HAL_GetTick();
  buzzer_active = 1;
}

/* 需在主循环中周期调用，检测响铃是否到时并关闭 */
void Buzzer_Update(void)
{
  if (buzzer_active && (HAL_GetTick() - buzzer_start_tick >= BUZZER_BEEP_MS))
  {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    buzzer_active = 0;
  }
}
