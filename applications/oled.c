#include "oled.h"
#include "i2c.h"
#include "u8g2.h"
#include <stdio.h>

static u8g2_t u8g2;

/* I2C 单次传输超时。原先写死 100ms：屏幕未接/总线异常时每次传输都要空等到超时，
   一帧有几十次传输，会把主循环阻塞到秒级，直接拖慢敲击反馈。 */
#define OLED_I2C_TIMEOUT_MS   10U

/* 连续失败后暂时不再尝试刷屏，避免每来一次振动都空等一遍 I2C 超时 */
#define OLED_FAIL_MAX         3U
static uint8_t oled_fail_cnt = 0;

/* u8g2 硬件 I2C 字节收发回调：把 u8g2 组好的一帧数据通过 hi2c1 发送给 SSD1306 */
static uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  static uint8_t buffer[32];
  static uint8_t buf_idx;
  uint8_t *data;

  switch (msg)
  {
    case U8X8_MSG_BYTE_SEND:
      data = (uint8_t *)arg_ptr;
      while (arg_int > 0)
      {
        buffer[buf_idx++] = *data;
        data++;
        arg_int--;
      }
      break;

    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC:
      break;

    case U8X8_MSG_BYTE_START_TRANSFER:
      buf_idx = 0;
      break;

    case U8X8_MSG_BYTE_END_TRANSFER:
      /* 必须检查返回值：屏未接时这里会返回错误，据此计数以便退避 */
      if (HAL_I2C_Master_Transmit(&hi2c1, u8x8_GetI2CAddress(u8x8), buffer, buf_idx,
                                  OLED_I2C_TIMEOUT_MS) != HAL_OK)
      {
        if (oled_fail_cnt < OLED_FAIL_MAX)
        {
          oled_fail_cnt++;
        }
        return 0;
      }
      oled_fail_cnt = 0;   /* 本次成功，清除退避 */
      break;

    default:
      return 0;
  }
  return 1;
}

/* u8g2 GPIO/延时回调：硬件 I2C 模式下无需手动控制 SDA/SCL，仅需提供延时 */
static uint8_t u8x8_gpio_and_delay_stm32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8;
  (void)arg_ptr;

  switch (msg)
  {
    case U8X8_MSG_DELAY_MILLI:
      HAL_Delay(arg_int);
      break;

    case U8X8_MSG_GPIO_AND_DELAY_INIT:
    case U8X8_MSG_DELAY_10MICRO:
    case U8X8_MSG_DELAY_100NANO:
    case U8X8_MSG_GPIO_I2C_CLOCK:
    case U8X8_MSG_GPIO_I2C_DATA:
    default:
      break;
  }
  return 1;
}

void OLED_Init(void)
{
  u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_hw_i2c, u8x8_gpio_and_delay_stm32);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetPowerSave(&u8g2, 0);
  u8g2_ClearBuffer(&u8g2);
  u8g2_SendBuffer(&u8g2);
}

void OLED_ShowCount(uint32_t count)
{
  char str[16];

  /* 屏幕未接或连续刷屏失败：直接跳过，不让 I2C 阻塞主循环 */
  if (oled_fail_cnt >= OLED_FAIL_MAX)
  {
    return;
  }

  snprintf(str, sizeof(str), "%lu", (unsigned long)count);

  u8g2_ClearBuffer(&u8g2);
  u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
  u8g2_DrawStr(&u8g2, 0, 20, "Vibration");
  u8g2_DrawStr(&u8g2, 0, 50, str);
  u8g2_SendBuffer(&u8g2);
}
