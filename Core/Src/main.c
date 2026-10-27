/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "can.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ws2812.h"
#include "vibration.h"
#include "oled.h"
#include "buzzer.h"
#include "diag.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 初始化阶段面包屑与失败记录统一放在 applications/diag.c。
   调试时 Watch 窗口看 diag_stage / diag_clock_ok / diag_i2c_ok / diag_can_ok；
   出错时蜂鸣器用"1 长声 + N 短声"报出阶段号 N（见 Diag_Fatal）。 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 当前已显示的颜色：0=熄灭 1=红 2=蓝，-1=未初始化（强制首次刷新） */
static int8_t led_state = -1;
/* 开关上一次的稳定位置；-1 强制开机时按开关档位刷新一次。
   注意：与 led_state 分开——led_state 是“实际显示的颜色”，可能被远程命令改写，
   switch_prev 只跟踪物理开关，用来做“边沿触发”，这样本地/远程互不持续压制。 */
static int8_t switch_prev = -1;
/* 远程（上位机）颜色命令缓存：由 CAN 接收中断写入，主循环消费 */
static volatile int8_t remote_color = -1;
static volatile uint8_t remote_pending = 0;
/* OLED 上一次显示的振动计数，用于避免每轮循环都重绘；UINT32_MAX 强制首次刷新 */
static uint32_t oled_shown_count = 0xFFFFFFFFU;

/* ---- 击打反馈（灯带亮度）----
   设计：不引入新颜色，用“亮度”区分状态——平时按 LED_BASE_PCT 满亮度常亮，被击打后
   LED_FLASH_MS 内熄灭（亮度 0），随后自动恢复常亮，形成一次“灭一下”的打击反馈。
   这样灯色始终是队伍色（红/蓝），红队蓝队各自色调统一，且无需改动 CAN 协议。
   两个状态各自独立，Led_Update() 是唯一修改灯带的地方：
     led_base_on     = 当前是否应该有灯色（0 = 熄灭档，或颜色未知）
     led_flash_until = 熄灭截止 tick；非 0 且未到时即为“击打熄灭”中 */
#define LED_BASE_PCT     100U      /* 平时亮度百分比（满亮度常亮） */
#define LED_FLASH_PCT      0U      /* 击打时亮度百分比（0 = 熄灭一下） */
#define LED_FLASH_MS     160U      /* 熄灭持续时间（在 150~200ms 区间） */

static uint8_t  led_base_on = 0;              /* 0=应为熄灭 1=应有颜色 */
static uint32_t led_flash_until = 0;          /* 0=不在击打熄灭中 */
static int8_t   led_last_color = -1;          /* 最近一次确定的颜色，用于"熄灭档"下也能给出击打反馈 */

/* 把“基础色 + 是否处于击打熄灭”真正刷到灯带；参数变化由调用方判断，避免每轮循环都发 DMA。
   颜色未知（led_state 仍为 -1，开机后尚未拨过开关）时按蓝色处理，仅用于产生可见的
   击打反馈；真正的颜色会在用户第一次拨动开关或收到 CAN 指令后确定。 */
static void Led_Update(void)
{
  uint8_t flash_off = (led_flash_until != 0U);

  if (!led_base_on)
  {
    /* 开关处在"熄灭"档：平时不亮。但被击打时要用最近一次的颜色闪一下，
       否则完全看不到击打反馈。从未有过颜色时按蓝色处理。 */
    if (flash_off)
    {
      uint8_t rc = (led_last_color == 1) ? 255U : 0U;
      uint8_t bc = (led_last_color == 1) ? 0U   : 255U;
      WS2812_Show(rc, 0U, bc, LED_BASE_PCT);
    }
    else
    {
      WS2812_Clear();
    }
  }
  else
  {
    /* 用独立 RGB 分量传参（不经结构体），红蓝互不干扰 */
    uint8_t r = (led_state == 1) ? 255U : 0U;
    uint8_t b = (led_state == 1) ? 0U   : 255U;
    WS2812_Show(r, 0U, b, flash_off ? LED_FLASH_PCT : LED_BASE_PCT);
  }
  WS2812_Send();
}
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint8_t Hse_Ready(void);
static uint8_t SystemClock_UseHsi(uint32_t pll_mul);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 统一的“设置颜色”入口：本地开关和远程命令都走这里。
   更新 led_state、刷新灯带、并把最新状态上报给上位机。
   注意：闪烁期间只更新 led_state 而不立刻刷灯，否则会把闪烁的亮色冲掉；
   等闪烁结束后由 Led_Update() 统一恢复成新颜色。 */
static void ApplyColor(int8_t color)
{
  led_state   = color;
  led_base_on = (color == 1 || color == 2) ? 1U : 0U;

  if (color == 1 || color == 2)
  {
    led_last_color = color;   /* 记下最近一次确定的颜色，供"熄灭"档的击打反馈使用 */
  }

  if (led_flash_until == 0U)   /* 击打熄灭期间不抢灯带，交给 Led_Update 收尾 */
  {
    Led_Update();
  }

  CAN_SendStatus((uint8_t)led_state, Vibration_GetCount());
}

/* 上电自检：闪一下蓝色再熄灭，用来确认"灯带硬件 + 驱动链"是活的。
   调试期发现这条链曾经完全不通（板子无 8MHz 晶振，原固件在 Error_Handler 死循环），
   保留这个 200ms 的上电指示，以后一上电就知道灯带好没好。 */
static void Led_BootIndicator(void)
{
  WS2812_Show(0U, 0U, 255U, 40U);   /* 蓝色、40% 亮度，不刺眼 */
  WS2812_Send();
  HAL_Delay(200);
  WS2812_Clear();
  WS2812_Send();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* 显式设置向量表基址为 Flash 起始地址。
     必要性：CubeMX 生成的 system_stm32f1xx.c 没有定义 USER_VECT_TAB_ADDRESS，
     因此 SCB->VTOR 保持复位默认值 0。正常从 Flash 启动时地址 0x00000000 会自动
     别名到 Flash，所以看不出问题；一旦启动模式被改动（例如为无线 ISP 烧录把
     BOOT0/BOOT1 拉高）别名就不再指向 Flash，CPU 会从错误地址取中断向量，
     表现为"程序能跑一段、一进中断就 HardFault"（HFSR.VECTTBL + CFSR.INVSTATE）。
     显式设置后不再依赖启动别名，任何启动模式下中断都能正确跳转。 */
  SCB->VTOR = FLASH_BASE;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  Diag_SetStage(DIAG_STAGE_HAL);
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  Diag_SetStage(DIAG_STAGE_CLOCK);
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  Diag_SetStage(DIAG_STAGE_GPIO);
  MX_GPIO_Init();
  Diag_Init();                    /* 蜂鸣器静音，准备报错码 */
  Diag_SetStage(DIAG_STAGE_DMA);
  MX_DMA_Init();
  Diag_SetStage(DIAG_STAGE_TIM);
  MX_TIM2_Init();
  WS2812_Init();                  /* 按实际 TIM2 时钟重算时序（72/64/48MHz 都能用） */
  Diag_SetStage(DIAG_STAGE_I2C);
  MX_I2C1_Init();
  Diag_RecordI2c(0U);             /* 走到这里说明 I2C 初始化通过（失败会在内部进 Error_Handler） */

  Diag_SetStage(DIAG_STAGE_CAN);
  MX_CAN_Init();
  Diag_RecordCan(diag_can_ok);    /* CAN 内部失败只记录状态码，不再卡死启动流程 */

  /* USER CODE BEGIN 2 */
  Diag_SetStage(DIAG_STAGE_VIB);
  Vibration_Init();

  /* 上电指示放在 OLED 之前：无屏时 I2C 要空等超时，先把灯带结果做出来 */
  Diag_SetStage(DIAG_STAGE_LED);
  Led_BootIndicator();

  Diag_SetStage(DIAG_STAGE_OLED);
  OLED_Init();
  Diag_SetStage(DIAG_STAGE_BUZZER);
  Buzzer_Init();

  Diag_SetStage(DIAG_STAGE_MAIN);   /* 初始化全部通过 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 读取开关：PB8 高 -> 红，PB9 高 -> 蓝，都不为高 -> 熄灭；同时为高时红色优先。 */
    int8_t want;
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_SET)
    {
      want = 1;   /* 红 */
    }
    else if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET)
    {
      want = 2;   /* 蓝 */
    }
    else
    {
      want = 0;   /* 灭 */
    }

    /* 本地开关：边沿触发——只在“拨动那一刻”作为一次本地命令生效。
       用 switch_prev（而非 led_state）比较，这样远程命令改了颜色后，
       只要开关没再动，就不会被轮询强行改回去。 */
    if (want != switch_prev)
    {
      HAL_Delay(20);   /* 去抖窗口 */

      int8_t confirm;
      if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_SET)
      {
        confirm = 1;
      }
      else if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET)
      {
        confirm = 2;
      }
      else
      {
        confirm = 0;
      }

      if (confirm == want)   /* 两次读数一致才认为稳定 */
      {
        switch_prev = want;      /* 记下开关新位置 */
        ApplyColor(want);        /* 本地命令生效 */
      }
    }

    /* 远程（上位机）命令：CAN 收到后由中断置位，这里消费。
       与本地开关是“最后操作优先”——谁最后动谁生效。 */
    if (remote_pending)
    {
      remote_pending = 0;
      int8_t cmd = remote_color;
      if (cmd >= 0 && cmd <= 2)
      {
        ApplyColor(cmd);         /* 远程命令生效 */
      }
    }

    /* 振动计数由 PA10 上升沿中断累加，这里只负责在计数变化时刷新 OLED 显示、蜂鸣一声 */
    uint32_t vib_count = Vibration_GetCount();
    if (vib_count != oled_shown_count)
    {
      /* 击打反馈：先灭灯（视觉最直接），再蜂鸣，再上报，最后刷 OLED。
         顺序很重要——把会阻塞的 OLED 放最后，任何一步都不会拖慢反馈。 */
      led_flash_until = HAL_GetTick() + LED_FLASH_MS;
      Led_Update();                /* 立刻熄灭一下 */
      Buzzer_Beep();
      oled_shown_count = vib_count;
      CAN_SendStatus((uint8_t)led_state, vib_count);
      OLED_ShowCount(vib_count);   /* 阻塞较久，放在最后 */
    }

    /* 击打熄灭到时：收尾并恢复常亮（若期间颜色被改过，这里会应用新颜色） */
    if (led_flash_until != 0U && (int32_t)(HAL_GetTick() - led_flash_until) >= 0)
    {
      led_flash_until = 0U;
      Led_Update();
    }
    Buzzer_Update();   /* 检测蜂鸣是否到时，到时则关闭 */

    HAL_Delay(10);   /* 轮询间隔 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief  尝试起振 HSE 并等待就绪。
  *         注意 STM32F1 复位后 HSE 是关闭的，必须先置 HSEON 再等 HSERDY。
  *         本板晶振确实能起振（HSERDY 会置位），但 PLL 高倍频不稳定：
  *         ×4(32MHz) 可用，×5 及以上必 HardFault，因此当前 HSE 分支被停用。
  * @retval 1=HSE 就绪  0=超时（无晶振或晶振故障）
  */
__attribute__((unused))   /* HSE 分支停用期间本函数无调用者，保留代码备恢复 */
static uint8_t Hse_Ready(void)
{
  uint32_t start = HAL_GetTick();

  /* 已经是旁路/直连模式（有源晶振）时会立刻置位 */
  if ((RCC->CR & RCC_CR_HSERDY) != 0U)
  {
    return 1U;
  }

  RCC->CR |= RCC_CR_HSEON;

  while ((RCC->CR & RCC_CR_HSERDY) == 0U)
  {
    if ((HAL_GetTick() - start) > 300U)
    {
      RCC->CR &= ~RCC_CR_HSEON;   /* 放弃 HSE，不留悬空的振荡器 */
      return 0U;
    }
  }
  return 1U;
}

/**
  * @brief  切到 HSI/2 × pll_mul 的 PLL 方案（无晶振时的兜底）。
  * @retval 1=成功 0=失败
  */
static uint8_t SystemClock_UseHsi(uint32_t pll_mul)
{
  RCC_OscInitTypeDef hsi = {0};

  hsi.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  hsi.HSIState = RCC_HSI_ON;
  hsi.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  hsi.PLL.PLLState = RCC_PLL_ON;
  hsi.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  hsi.PLL.PLLMUL = pll_mul;

  return (HAL_RCC_OscConfig(&hsi) == HAL_OK) ? 1U : 0U;
}

/**
  * @brief System Clock Configuration
  *
  * 策略（按优先级）：
  *   1. HSE 8MHz × PLL9 = 72MHz —— 板上有晶振时最精确
  *   2. HSI/2 × PLL16   = 64MHz —— 无晶振兜底（本板实测走这条）
  *   3. HSI/2 × PLL12   = 48MHz —— 再退一步的保底
  * 关键：无论最终主频多少，WS2812_Init() 都会按实际时钟重算 ARR/CCR，
  * 所以灯带在三条分支下都能正确出码，不会因降频而失效。
  * 结果记录在 diag_clock_ok：1=72M(HSE)  3=64M(HSI)  2=48M(HSI)
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* ===== 暂时停用 HSE，直接使用内部 HSI =====
     原因（实机验证）：本板 8MHz 晶振在 PLL 高倍频下不稳定。
       HSE ×4 = 32MHz -> 正常
       HSE ×5 = 40MHz -> HardFault
       HSE ×8 = 64MHz -> HardFault
       HSE ×9 = 72MHz -> HardFault（实测停在 0x080043C8 故障处理程序，
                                  RCC_CFGR=0x001D3F0A 已切到 HSE 72MHz 后崩）
     而**同样 64MHz 走 HSI 可以完整运行**，故先用 HSI 保证功能可用。
     怀疑晶振负载电容与规格不匹配；硬件排查后可恢复下面的 HSE 分支。
     恢复方法：把 #if 0 改成 #if 1，并把 HSE 的 PLLMUL 降为 RCC_PLL_MUL4。 */
#if 0
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};

  if (Hse_Ready() != 0U)
  {
    /* ---- 首选：HSE × 9 = 72MHz，WS2812 时序最精确 ---- */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) == HAL_OK)
    {
      diag_clock_ok = 1U;   /* 72MHz */
    }
    else if (SystemClock_UseHsi(RCC_PLL_MUL16) != 0U)
    {
      diag_clock_ok = 3U;   /* 64MHz */
    }
    else if (SystemClock_UseHsi(RCC_PLL_MUL12) != 0U)
    {
      diag_clock_ok = 2U;   /* 48MHz */
    }
    else
    {
      Error_Handler();
    }
  }
  else
#endif
  {
    /* ---- 内部 HSI：HSI/2 × 16 = 64MHz（实测可用）---- */
    if (SystemClock_UseHsi(RCC_PLL_MUL16) != 0U)
    {
      diag_clock_ok = 3U;   /* 64MHz */
    }
    else if (SystemClock_UseHsi(RCC_PLL_MUL12) != 0U)
    {
      diag_clock_ok = 2U;   /* 48MHz */
    }
    else
    {
      Error_Handler();
    }
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  /* 主频最高 72MHz，Flash 需 2 个等待周期；更低主频下同样安全 */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* CAN RX FIFO0 收到消息时回调（在 USB_LP_CAN1_RX0_IRQHandler 上下文）。
   只解析颜色控制帧 CAN_CMD_STD_ID，取 data[0] 作为目标颜色，交给主循环处理。 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_rx)
{
  CAN_RxHeaderTypeDef rxHeader;
  uint8_t rxData[8];

  if (HAL_CAN_GetRxMessage(hcan_rx, CAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK)
  {
    return;
  }

  if (rxHeader.IDE == CAN_ID_STD && rxHeader.StdId == CAN_CMD_STD_ID && rxHeader.DLC >= 1)
  {
    if (rxData[0] <= 2)          /* 只接受 0/1/2 */
    {
      remote_color = (int8_t)rxData[0];
      remote_pending = 1;
    }
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* 蜂鸣器报错码：1 长声 + N 短声，N = 出错阶段号（DIAG_STAGE_xxx）。
     diag_stage 变量也可在 Watch 窗口查看。Diag_Fatal 内部死循环。
     阶段对照：1=HAL_Init 2=时钟 3=GPIO 4=DMA 5=TIM2 6=I2C1 7=CAN
               8=Vibration 9=OLED 10=Buzzer 11=灯带自检 12=主循环 */
  Diag_Fatal(diag_stage);
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
