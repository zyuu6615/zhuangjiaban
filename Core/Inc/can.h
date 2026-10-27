/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.h
  * @brief   This file contains all the function prototypes for
  *          the can.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H__
#define __CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern CAN_HandleTypeDef hcan;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_CAN_Init(void);

/* USER CODE BEGIN Prototypes */

/* 状态上报帧（MCU -> 上位机）的标准 CAN ID */
#define CAN_STATUS_STD_ID   0x100U

/* 颜色控制帧（上位机 -> MCU）的标准 CAN ID：data[0] = 0灭/1红/2蓝 */
#define CAN_CMD_STD_ID      0x200U

/* 配置过滤器并启动 CAN（在 MX_CAN_Init 之后调用一次） */
void CAN_Start(void);

/* 将当前灯状态与振动计数打包成一帧发送到上位机。
   data[0]   = 灯状态 (0=灭 1=红 2=蓝)
   data[1..4]= 振动计数 (uint32, 小端)
   返回 HAL_OK 表示已放入发送邮箱。 */
HAL_StatusTypeDef CAN_SendStatus(uint8_t led_state, uint32_t vib_count);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __CAN_H__ */

