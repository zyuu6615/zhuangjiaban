/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.c
  * @brief   This file provides code for the configuration
  *          of the CAN instances.
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
/* Includes ------------------------------------------------------------------*/
#include "can.h"
#include "diag.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

CAN_HandleTypeDef hcan;

/* CAN init function */
void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  /* 波特率 = 36MHz(APB1) / Prescaler / (1 + BS1 + BS2) = 36M / 4 / 18 = 500 kbps
     采样点 = (1 + BS1) / 18 = 88.9%，共 18 个 TQ，抗干扰余量充足 */
  hcan.Init.Prescaler = 4;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_2TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_15TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = DISABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  /* 初始化失败不再让整机卡在 Error_Handler：记录状态码后继续启动。
     灯带/振动/OLED 这些功能不依赖 CAN，CAN 有问题应该只影响 CAN。
     失败原因（diag_can_ok）：1=HAL_ERROR（等 INAK 超时）2=HAL_BUSY 3=HAL_TIMEOUT */
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    diag_can_ok = 1U;
    return;
  }
  diag_can_ok = 0U;   /* 0 = 初始化成功，便于调试时一眼确认 */
  /* USER CODE BEGIN CAN_Init 2 */
  CAN_Start();
  /* USER CODE END CAN_Init 2 */

}

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspInit 0 */

  /* USER CODE END CAN1_MspInit 0 */
    /* CAN1 clock enable */
    __HAL_RCC_CAN1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**CAN GPIO Configuration
    PA11     ------> CAN_RX
    PA12     ------> CAN_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN CAN1_MspInit 1 */

  /* USER CODE END CAN1_MspInit 1 */
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{

  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspDeInit 0 */

  /* USER CODE END CAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CAN1_CLK_DISABLE();

    /**CAN GPIO Configuration
    PA11     ------> CAN_RX
    PA12     ------> CAN_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

  /* USER CODE BEGIN CAN1_MspDeInit 1 */

  /* USER CODE END CAN1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* 配置一个“全通过”过滤器并启动 CAN 外设。
   仅发送也需要 HAL_CAN_Start 将 CAN 从初始化模式切到正常模式。 */
void CAN_Start(void)
{
  CAN_FilterTypeDef sFilterConfig;

  sFilterConfig.FilterBank = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0x0000;
  sFilterConfig.FilterIdLow = 0x0000;
  sFilterConfig.FilterMaskIdHigh = 0x0000;   /* 掩码全 0 -> 接收所有 ID */
  sFilterConfig.FilterMaskIdLow = 0x0000;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;

  /* 滤镜/启动/中断激活失败同样只记录状态码，不阻塞整机启动。
     调试灯带时 CAN 总线往往是空接的，让它把整机拖死没有意义。 */
  if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK)
  {
    diag_can_ok = 10U;
    return;
  }

  if (HAL_CAN_Start(&hcan) != HAL_OK)
  {
    diag_can_ok = 11U;
    return;
  }

  /* 开启 RX FIFO0 消息挂起中断，用于接收上位机的颜色控制帧 */
  if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
  {
    diag_can_ok = 12U;
    return;
  }
  HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
}

HAL_StatusTypeDef CAN_SendStatus(uint8_t led_state, uint32_t vib_count)
{
  CAN_TxHeaderTypeDef txHeader;
  uint8_t data[8] = {0};
  uint32_t txMailbox;
  uint32_t start;

  txHeader.StdId = CAN_STATUS_STD_ID;
  txHeader.ExtId = 0;
  txHeader.IDE = CAN_ID_STD;
  txHeader.RTR = CAN_RTR_DATA;
  txHeader.DLC = 5;
  txHeader.TransmitGlobalTime = DISABLE;

  data[0] = led_state;
  data[1] = (uint8_t)(vib_count & 0xFFU);
  data[2] = (uint8_t)((vib_count >> 8) & 0xFFU);
  data[3] = (uint8_t)((vib_count >> 16) & 0xFFU);
  data[4] = (uint8_t)((vib_count >> 24) & 0xFFU);

  /* 等待有空闲发送邮箱，最多 10ms，避免邮箱满时静默丢帧 */
  start = HAL_GetTick();
  while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0U)
  {
    if ((HAL_GetTick() - start) > 10U)
    {
      return HAL_TIMEOUT;
    }
  }

  return HAL_CAN_AddTxMessage(&hcan, &txHeader, data, &txMailbox);
}

/* USER CODE END 1 */
