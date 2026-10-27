#include "vibration.h"
#include "main.h"

/* 同一次振动可能在短时间内产生多个上升沿，此窗口内的后续沿一律忽略 */
#define VIBRATION_DEBOUNCE_MS   50U

static volatile uint32_t vibration_count = 0;
static volatile uint32_t last_trigger_tick = 0;

void Vibration_Init(void)
{
  vibration_count = 0;
  last_trigger_tick = 0;
}

uint32_t Vibration_GetCount(void)
{
  return vibration_count;
}

void Vibration_Reset(void)
{
  vibration_count = 0;
}

/* PA10 上升沿触发；HAL_GPIO_EXTI_IRQHandler 内部会调用此弱函数 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_10)
  {
    uint32_t now = HAL_GetTick();

    if ((now - last_trigger_tick) >= VIBRATION_DEBOUNCE_MS)
    {
      vibration_count++;
    }
    last_trigger_tick = now;
  }
}
