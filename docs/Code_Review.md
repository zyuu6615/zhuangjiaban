# Touch_led 项目代码分析报告

分析对象：整个仓库（`Core/`、`applications/`、构建系统、CubeMX 配置、文档）。
分析方式：全量阅读 + 实际编译验证（`cmake --preset Release`，clean-first 全量重编，165 个编译单元，**0 warning / 0 error**）。

---

## 1. 项目概述

| 项目 | 内容 |
|------|------|
| MCU | STM32F103C8T6（Cortex-M3，64 KB Flash / 20 KB RAM） |
| 时钟 | HSE 8 MHz × PLL9 = **72 MHz**；APB1 = 36 MHz（CAN 时钟源） |
| 功能 | 本地拨动开关 / CAN 远程控制 WS2812 灯色；击打检测计数并在 OLED 显示 + 蜂鸣提示；通过 CAN 上报状态 |
| 构建 | CMake + Ninja + arm-none-eabi-gcc 15.2.1，CubeMX 生成骨架（`DeletePrevious=true`） |
| 烧录 | OpenOCD + ST-Link（`openocd.cfg`） |

### 引脚分配（与 `.ioc` 一致，已逐一核对）

| 引脚 | 功能 | 备注 |
|------|------|------|
| PA0 | TIM2_CH1 → WS2812 数据 | PWM+DMA1_Ch5，ARR=89 → 1.25 µs/bit |
| PA10 | EXTI10 上升沿 | 击打/振动检测，下拉输入 |
| PA11 / PA12 | CAN1_RX / CAN1_TX | 500 kbps |
| PB5 | 蜂鸣器（推挽输出） | 见 D2：极性注释已过期 |
| PB6 / PB7 | I2C1_SCL / SDA | SSD1306 OLED，100 kHz |
| PB8 / PB9 | 本地开关输入 | 下拉；高=红 / 高=蓝 / 都低=灭，红优先 |

### 模块职责

- `Core/Src/main.c` — 主循环：开关去抖与边沿检测、远程命令消费、振动计数变化处理、CAN 状态上报。
- `applications/ws2812.c` — 预计算 CCR 数组 + DMA 逐周期搬运，硬件精确产生归零码波形。
- `applications/vibration.c` — EXTI 回调计数，带 50 ms 滑动去抖窗口。
- `applications/oled.c` — u8g2 + 硬件 I2C 回调，显示 `Vibration` 与计数值。
- `applications/buzzer.c` — 非阻塞 30 ms 蜂鸣状态机。
- `Core/Src/can.c` — 500 kbps 初始化、全通过过滤器、状态帧发送（`0x100`）。

---

## 2. 做得好的地方

1. **WS2812 时序余量充足**（核对计算）：
   - TIM2 = 72 MHz，ARR=89 → 周期 90 计数 = 1.25 µs（精确等于 WS2812 位周期）。
   - `CODE0=30` → 高电平 (30+1)/72 MHz ≈ **431 ns**（规格 0.35 µs ±150 ns，在范围内）。
   - `CODE1=60` → 高电平 (60+1)/72 MHz ≈ **847 ns**（规格 0.7~1.0 µs，在范围内）。
   - 复位段 48×1.25 µs = **60 µs** ≥ 50 µs 锁存要求。
   - 整帧 432 bit × 1.25 µs ≈ **540 µs**，远小于主循环 10 ms 周期 → 正常路径下不会 DMA 重入。
2. **`ws2812_buf` 正确落在 RAM**（已用 map/ELF 验证：`.bss` @ `0x20000194`，864 B）。它没有被 `-Os` 提升为 `const` 放进 Flash —— 否则 DMA 从 Flash 读取会在时序上出问题。这点是隐含的坑，目前是安全的。
3. **无堆依赖**：`u8g2_m_16_8_f` 使用函数内 `static uint8_t buf[1024]`（ELF 符号 `buf.219`，1024 B），未启用 `U8G2_USE_DYNAMIC_ALLOC`，因此不依赖 `_sbrk`/heap，避免了嵌入式上常见的堆问题。
4. **OLED I2C 回调缓冲区是安全的**（曾怀疑溢出，已逐层验证）：u8g2 的 `u8x8_cad_ssd13xx_i2c` 把数据按 24 字节分片，每片经 `u8x8_i2c_data_transfer` 先发 1 字节控制字再发 ≤24 字节，故单次 `U8X8_MSG_BYTE_SEND` 最多写 **25 B**，而缓冲区为 32 B，有余量。
5. **u8g2 裁剪正确**：仅 `u8g2_font_ncenB14_tr`（2088 B）被链接进固件，40 MB 的 `u8g2_fonts.c` 其余内容被 `--gc-sections` 丢弃。
6. **本地/远程仲裁设计有想法**：用独立的 `switch_prev`（只跟踪物理开关）而非 `led_state` 做边沿比较，避免"轮询把远程设的颜色强行改回去"。这是正确的思路（细微时序问题见 D3）。
7. **CAN 时序参数正确**：APB1 36 MHz ÷ 4 ÷ (1+15+2) = **恰好 500 kbps**，采样点 88.9%（16/18），SJW=2，符合 `docs/CAN_Protocol.md`。
8. **注释质量高**：关键决策（去抖窗口与蜂鸣时长的关系、DMA 为何可重复调用、仲裁语义）都有"为什么"而非"是什么"的注释，中文表达清晰。

---

## 3. 资源占用（实测）

| 指标 | Release | Debug（历史产物） |
|------|---------|------------------|
| FLASH | **18 104 B / 64 KB = 27.6 %** | — |
| RAM | **4 360 B / 20 KB = 21.3 %** | — |
| `.text` / `.rodata` | 15 416 B / 2 312 B | — |
| `.data` / `.bss` | 100 B / 2 720 B | — |
| 空闲余量 | Flash ≈ 45 KB，RAM ≈ 15.6 KB | — |

RAM 主要占用：`ws2812_buf` 864 B、u8g2 framebuffer 1024 B、`u8g2_t` 148 B、newlib `__sf` 312 B、链接脚本保留 heap 512 B + stack 1024 B。

**结论：资源非常宽裕**，Flash 剩 70 %、RAM 剩 78 %，有充足空间加看门狗、Bootloader、更多字体或协议功能。

> 注意：仓库根目录没有 `Touch_led.bin` / `.hex`，`build/` 被 `.gitignore` 忽略，需自行 `objcopy` 产出烧录文件。

---

## 4. 问题清单

### 🔴 D1：`.ioc` 与源码不一致 —— 用 CubeMX 重新生成会**直接破坏 CAN 通信**

`.ioc`（CubeMX 的唯一真源，且 `ProjectManager.DeletePrevious=true` + `KeepUserCode=true`）与 `can.c` 已脱节：

| 参数 | `Touch_led.ioc` | `Core/Src/can.c`（实际生效） |
|------|-----------------|------------------------------|
| `CAN.BS1` | `CAN_BS1_5TQ` | `CAN_BS1_15TQ` |
| `CAN.Prescaler` | `4` | `4` |
| 推算波特率 | 36 M ÷ 4 ÷ 8 = **1.125 Mbps** | 36 M ÷ 4 ÷ 18 = **500 kbps** ✅ |
| `CAN.CalculateBaudRate` | `1125000` | — |

只要在 CubeMX 里点一次 **Generate Code**，`can.c` 的 `Prescaler`/`TimeSeg1` 会被改回 `4`/`BS1_5TQ`，CAN 变成 1.125 Mbps，与上位机及 `docs/CAN_Protocol.md` 声称的 500 kbps 完全不兼容 —— 而且这个改动发生在 `MX_CAN_Init` 的**非 USER CODE 段**，`KeepUserCode` 保护不了它。

同时 `USB_LP_CAN1_RX0_IRQn` 在 `.ioc` 的 NVIC 里**未勾选**（源码是靠 `stm32f1xx_it.c` 的 USER CODE 段 + `CAN_Start()` 里运行时 `HAL_NVIC_EnableIRQ` 补上的）。这本身是能工作的（因为 `CAN_Start()` 显式使能了），但"中断勾选状态"与 `.ioc` 不一致，属于同类隐患。

**建议**：打开 `Touch_led.ioc`，把 CAN 的 `BS1` 改成 `15TQ`（`Prescaler=4`、`BS2=2TQ`、`SJW=2TQ` 保持不变），让 CubeMX 自行重算出 500000；并在 NVIC 页勾上 `USB_LP_CAN1_RX0_IRQn`。改完确认 `CAN.CalculateBaudRate=500000` 再保存提交。

---

### 🔴 D2：CAN 总线关闭（bus-off）后**永不自恢复**

`can.c` 中：

```c
hcan.Init.AutoBusOff = DISABLE;
hcan.Init.AutoRetransmission = DISABLE;
```

且全工程**没有实现 `HAL_CAN_ErrorCallback`**。后果：上位机未就绪、线缆未接、终端电阻缺失等导致发送无 ACK 时，TEC 会累积到 256 进入 **bus-off**；`AutoBusOff=DISABLE` 表示不自动恢复，软件也没有任何恢复逻辑，于是 **MCU 后续既收不到也不发出任何 CAN 帧，直到断电重启**。现场表现就是"灯板突然不理上位机了，重新插拔电源才好"。

**建议**（任选，推荐 1+2）：

1. 打开 `AutoBusOff = ENABLE`（HAL 会在检测到 bus-off 时按协议自动恢复）。
2. 实现错误回调做兜底恢复：

   ```c
   void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
   {
     if (HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF) {
       HAL_CAN_Stop(hcan);
       HAL_CAN_Start(hcan);
       HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
     }
   }
   ```

3. 顺带考虑打开 `AutoRetransmission`（单节点/干扰环境下更符合"尽力送达"语义）。

---

### 🟠 D3：远程命令存在"丢失"竞态窗口（`main.c`）

```c
if (remote_pending)
{
  remote_pending = 0;        // ① 先清标志
  int8_t cmd = remote_color; // ② 再读数据
  ...
}
```

CAN 接收中断优先级为 **1**，高于主循环，可在 ① 与 ② 之间打断：此时 ISR 写入新的 `remote_color = X` 并置 `remote_pending = 1`，主循环醒来后 ② 读到的其实是 X（还是新值），但 ① 已经把 `remote_pending` 清成 0 —— **紧接着到来的那一条命令（第三次写入）会被永久丢弃**，因为标志已被清掉而没有第二次机会。

此外 `remote_color`(int8_t) 与 `remote_pending`(uint8_t) 的更新不是原子的，属于非严格正确的跨上下文共享。

**建议**：把两步顺序对调（先取值再清标志，最差情况只是重复应用同一条命令，语义安全），或者用一个 `volatile uint8_t` 的"序号/信箱"做单字原子交换：

```c
int8_t cmd = remote_color;   // 先读数据
remote_pending = 0;          // 后清标志
```

---

### 🟠 D4：`HAL_Delay(20)` 去抖阻塞主循环，削弱"最后操作优先"语义

`main.c` 在开关变化时 `HAL_Delay(20)` 阻塞等待去抖确认。这 20 ms 内主循环停摆（远程命令消费、`Buzzer_Update`、OLED 刷新都被推迟）。

更微妙的是**仲裁顺序**：若远程命令恰在这 20 ms 窗口内被消费并 `ApplyColor(cmd)`，随后去抖确认通过又会调用 `ApplyColor(want)` 把远程颜色覆盖掉 —— 结果是"**本地那次拨动实际上晚于远程命令生效**"，与 `docs/CAN_Protocol.md` 第 3 节承诺的 last-command-wins 在字面上相反。虽然窗口只有 20~30 ms（人几乎感知不到），但协议语义上是个真实偏差。

**建议**：改成非阻塞状态机（记录候选值 + 起始 tick，10 ms 轮询里比较 `HAL_GetTick()`），既消除阻塞又让仲裁严格按"生效时刻"排序。

---

### 🟠 D5：阻塞式 I2C 的 100 ms 超时会明显拖慢主循环

`oled.c` 的 `U8X8_MSG_BYTE_END_TRANSFER` 用 `HAL_I2C_Master_Transmit(..., 100)` 且**不检查返回值**。一帧 128×64 全屏刷新被切成多次 25 B 传输，每次都有独立的 100 ms 上限。若 I2C 总线被拉低卡死（SDA 被从机钳位是常见故障），单次 `OLED_ShowCount` 最坏可阻塞数百毫秒，期间 CAN 响应、蜂鸣、开关全部停摆。

**建议**：缩短超时（如 10 ms），增加返回值检查与 I2C 总线恢复（9 个 SCL 脉冲 + STOP）逻辑，或改为 `HAL_I2C_Master_Transmit_IT/DMA`。

---

### 🟡 D6：`buzzer.c` 的注释与实际极性**正好相反**（且历史提交也反复过）

`gpio.c` 上电把 PB5 置 **RESET**，`buzzer.c` 里 `Buzzer_Init()` 也置 RESET，`Buzzer_Beep()` 拉 **SET** 触发，`Buzzer_Update()` 拉 RESET 关闭。功能上是自洽的，但注释全是反的：

```c
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);   /* 拉高 = 不触发 */
...
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);     /* 这里才拉高，却是"触发" */
```

`buzzer.h` 和 `Buzzer_Beep` 的注释也写着"拉低开始"。**实际是低电平静音、高电平鸣响**（即高电平触发）。git 历史显示极性在 `f15b3aa` 之后又被改过一次（`applications/buzzer.c` 未提交改动正是这次翻转），说明这里来回改过 —— 恰恰是注释没跟着改导致的。

**建议**：确认硬件（若为低电平触发的有源蜂鸣器，现在固件是**反的**：静音时输出高电平），并把三处注释统一改成实际极性。

---

### 🟡 D7：CAN 过滤器"全通过"，软件层过滤

`CAN_Start()` 把掩码设为全 0，**总线上所有 ID 都会进 FIFO0 并触发 `USB_LP_CAN1_RX0_IRQn`**，再由 `HAL_CAN_RxFifo0MsgPendingCallback` 判断是否 `0x200`。当前总线上只有两类帧，开销可忽略。但一旦接入 RoboMaster 那种高负载总线（大量 0x201 等 ID 以 1 kHz 刷屏），MCU 会为每一帧都进一次中断后发现"不是给我的再丢掉"，同时 FIFO0（3 级）可能被无关帧占满，导致**真正的 `0x200` 被硬件静默丢弃**。

**建议**：既然协议只定义了 `0x200`，把过滤器改成只收 `0x200`（ID + 掩码），既省中断又避免 FIFO 被挤占。若确实需要保持"接收一切"的调试能力，再单独开一个过滤器组 + FIFO1。

---

### 🟡 D8：中断优先级设置偏激进

| 中断 | 优先级 |
|------|--------|
| DMA1_Channel5（WS2812） | 0 |
| TIM2（未使用中断） | 0 |
| CAN RX0 | 1 |
| EXTI15_10（振动） | 2 |
| SysTick（HAL 时基） | 15（最低） |

把 SysTick 放到最低是 CubeMX 默认，本身没错（保证 HAL 时基不被自己的 ISR 阻塞）。但 DMA/TIM2 用 0，意味着 WS2812 的 DMA 完成中断可以抢占一切。当前各 ISR 都很短（只做标志位），实际影响很小；`CAN_SendStatus` 在**主循环**里轮询邮箱时若被 DMA 抢占，最坏多等一次 540 µs 传输 —— 仍远小于 10 ms 超时，可接受。

**结论**：现状可用，属于"能跑但优先级语义不够清晰"。若后续在 ISR 里加逻辑，建议把 DMA/TIM2 降到 1~2，与 CAN 平级或更低。

---

### 🟡 D9：其他观察

1. **无看门狗（IWDG）**：全工程未启用。配合 D5（I2C 可能长时间阻塞）和 `Error_Handler()` 的 `while(1)`，一旦卡死只能断电。
2. **`Vibration_Reset()` 是死代码**：定义了但从未被调用；`vibration.h` 暴露该 API，CAN 协议里也没有"清零计数"命令（`0x200` 只用 `data[0]`）。若上位机需要清零，可在 `0x200` 里约定 `data[1]` 或新增 ID。
3. **无固件版本上报**：CAN 协议里没有版本字段，现场排查无法确认烧的是哪一版。
4. **`Error_Handler()` 直接 `__disable_irq(); while(1);`**：任一外设初始化失败（含 CAN）都会让整个灯板变砖，而灯和 OLED 本可独立工作。建议至少让灯进入一个可辨识的"故障闪烁"状态。
5. **u8g2 采用 `file(GLOB ...)` 全量编译**：40 MB / 165 个编译单元，全量重编耗时约 4 分钟（本次实测），而实际只用到一个字体和一个显示驱动。可用 `U8G2_USE_SELECTIVE` 风格的裁剪：只保留 `u8g2_fonts.c` 里需要的字体，或把未用的 `u8x8_d_*.c` 从构建中排除，把编译时间降到十几秒。注意这属于"开发体验"优化，不影响固件体积（`--gc-sections` 已经兜底）。
6. **`HAL_CAN_RxFifo0MsgPendingCallback` 每次只读 1 帧**：FIFO0 有 3 级，一次中断处理 1 帧。正常流量下没问题（该函数返回后只要 FIFO 非空会再次触发中断），但高流量时应改成 `while (HAL_CAN_GetRxFifoFillLevel(...) > 0)` 循环取空。

---

## 5. 建议的修复顺序

| 优先级 | 项目 | 理由 |
|--------|------|------|
| 1 | **D1** 修 `.ioc` 的 CAN BS1/NVIC | 一次误操作就能让 CAN 彻底失效，且会随代码提交扩散 |
| 2 | **D2** 加 bus-off 恢复 | 现场"莫名失联"的头号嫌疑 |
| 3 | **D3** 调整 `remote_pending`/`remote_color` 顺序 | 一行改动，消除丢命令竞态 |
| 4 | **D6** 确认并统一蜂鸣器极性 + 注释 | 需硬件确认；若是低电平触发则当前固件行为是反的 |
| 5 | **D7** 收窄 CAN 过滤器到 `0x200` | 为接入高负载总线做准备 |
| 6 | **D4 / D5** 非阻塞去抖、I2C 超时与恢复 | 健壮性提升，配合 D9-1 看门狗 |
| 7 | **D9-2/3** 清零命令、版本上报 | 协议完善 |

---

## 6. 附：本次验证方式

```powershell
cmake --preset Release
cmake --build --preset Release --clean-first   # 165 单元，0 warning / 0 error
arm-none-eabi-size -A build/Release/Touch_led.elf
arm-none-eabi-nm --print-size --size-sort --radix=d build/Release/Touch_led.elf
```

已核对项：

- ✅ 全量编译通过，自有代码（`Core/` + `applications/` 非 u8g2 部分）零警告。
- ✅ `ws2812_buf` 位于 RAM（`.bss` @ `0x20000194`，864 B）。
- ✅ u8g2 framebuffer 为静态分配（`buf.219`，1024 B），无 heap 依赖。
- ✅ 仅 `u8g2_font_ncenB14_tr` 被链接（2088 B）。
- ✅ WS2812 的 CODE0/CODE1/复位段时长均在 WS2812B 规格内。
- ✅ CAN 500 kbps / 采样点 88.9 % 计算无误。
- ❌ `.ioc` 中的 CAN 参数与源码不符（见 D1）。
