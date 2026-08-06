---

# STM32H7 多传感器工厂测试（Factory-Test）项目完全解析

---

# 第一部分 · 整体项目介绍

## 1.1 这是什么项目

这是一个 **STM32H7 系列 AIoT 多传感器综合测试板（产线工厂测试固件）**。

它的核心用途是：**在产线上，把一块集成了 8 种传感器/模块的开发板插上电，通过串口终端敲命令，逐个测试每个外设是否焊接正常、通信正常，并打印 PASS / FAIL 结果**。同时它还是一个功能演示程序——常态下自动周期采集温湿度、光照、CO₂，实时刷新到 LCD 屏上。

所以这个工程有**两条并行的主线**：

| 主线 | 说明 | 入口 |
|---|---|---|
| **A. 命令行测试主线** | 串口终端敲 `1`~`8`、`help`、`auto`、`show`，逐项测试外设 | `console_run()` |
| **B. 自动采集展示主线** | 定时器软计数到期 → 采集传感器 → 刷新 LCD | `timer_collect_data()` |
| **C. 人机交互主线** | 3 个按键：翻页 / 开关全部指示灯与蜂鸣器 | `key_function()` + EXTI 中断 |

## 1.2 MCU 与外设资源全表

**MCU 型号**：`STM32H7` 系列（从 `stm32h7xx_hal.h`、`PWR_LDO_SUPPLY`、`RCC_CLOCKTYPE_D3PCLK1`、`APB4CLKDivider` 判断为 H7 单核系列，工程名 `factory-test`）。

**时钟配置极其特殊**（后面详解）：**不用 PLL，直接用 HSI 64MHz 当系统时钟**。

### 外设分配总表

| 外设 | 型号/用途 | 接口 | 引脚/参数 | 波特率/时序 |
|---|---|---|---|---|
| **BH1750** | 数字光照强度传感器 | **I2C3** | SDA=PC9, SCL=PA8 | Timing=0x00707CBB |
| **SCD41 (SCD4x)** | CO₂ 二氧化碳传感器 | **I2C2** | — | Timing=0x00707CBB |
| **DHT11** | 温湿度传感器 | **单总线 GPIO** | **PD10** | 微秒时序，TIM1 计时 |
| **BMP280** | 大气压传感器（智能模块，串口输出） | **USART10** | — | 115200 |
| **超声波测距** | 距离测量模块 | **UART5** | — | **9600** |
| **GNSS** | 北斗/GPS 卫星定位 | **UART4** | — | 115200 |
| **EB21 星闪模块** | 星闪（NearLink）通信模块 | **UART7** | XS_BUSY=PE4, XS_DOUT=PE5, XS_POWER_IND=PE6, XS_RESET=PC13, XS_POWER_CL=PF8 | 115200 |
| **LCD 串口屏** | 组态串口屏（SET_TXT/JUMP 指令集） | **USART3** | — | 115200 |
| **调试控制台** | printf 重定向 + 命令行输入 | **USART1** | — | 115200 |
| **三色指示灯** | GREEN / YELLOW / RED | GPIO 输出 | PA15 / PC12 / PD2 | — |
| **蜂鸣器/PWM** | PWM 输出 | **TIM3_CH3** | PWMOUT=PB0 | 1kHz，占空比 0~99 |
| **3 个按键** | KEY1/KEY2/KEY3 | **EXTI 外部中断** | PE15 / PE14 / PE13（共用 EXTI15_10） | — |
| **TIM1** | 微秒级延时基准（给 DHT11 用） | — | PSC=63, ARR=65535 → **1MHz，1 计数=1μs** | — |
| **TIM2** | 系统节拍定时器（软件分频器） | — | PSC=6399, ARR=9999 → **1 秒中断一次** | — |

**关键定时器计算（必须理解）：**

```
TIM1: 时钟源 64MHz(APB分频后)，Prescaler = 64-1 = 63
      → 计数频率 = 64MHz / (63+1) = 1MHz
      → 每计数 1 次 = 1 微秒。Period=65535，用作自由跑的微秒计数器

TIM2: Prescaler = 6399, Period = 9999
      → 计数频率 = 64MHz / (6399+1) = 10000 Hz = 10kHz
      → 溢出周期 = (9999+1) / 10000 Hz = 1 秒
      → 每 1 秒进一次 HAL_TIM_PeriodElapsedCallback

TIM3: Prescaler = 64-1 = 63, Period = 100-1 = 99
      → 计数频率 = 1MHz，PWM 周期 = 100 计数 = 100μs = 10kHz
      → CCR3 (Pulse) 取值 0~99 即占空比 0%~99%
```

## 1.3 工程目录结构

```
factory-test/
├── factory-test.ioc          ← CubeMX 工程配置文件（双击可重新生成代码）
│
├── Core/                     ← CubeMX 自动生成区（几乎不手改）
│   ├── Inc/
│   │   ├── main.h            ← 【重要】所有引脚宏定义（KEY1_Pin/GREEN_Pin...）
│   │   ├── i2c.h  usart.h  tim.h  gpio.h
│   │   ├── stm32h7xx_hal_conf.h   ← HAL 模块裁剪开关
│   │   └── stm32h7xx_it.h
│   └── Src/
│       ├── main.c            ← 【核心】含大量用户手写代码
│       ├── i2c.c             ← MX_I2C2_Init / MX_I2C3_Init
│       ├── usart.c           ← 6 个串口的初始化
│       ├── tim.c             ← TIM1/TIM2/TIM3 初始化
│       ├── gpio.c            ← GPIO + EXTI 初始化
│       ├── stm32h7xx_it.c    ← 中断向量入口（ISR）
│       ├── stm32h7xx_hal_msp.c ← HAL 底层引脚/时钟映射
│       └── system_stm32h7xx.c
│
├── BSP/                      ← 【重点】用户手写的板级支持包
│   ├── Inc/  (12 个头文件)
│   └── Src/
│       ├── queue.c           ← 环形缓冲队列（串口接收缓冲）
│       ├── split_argv.c      ← 命令行参数解析（状态机）
│       ├── usr_uart.c        ← printf 重定向到 USART1
│       ├── console.c         ← 命令行控制台框架（链表式命令注册）
│       ├── list.h            ← Linux 风格双向链表（纯头文件）
│       ├── BH1750.c          ← I2C 光照传感器驱动
│       ├── SCD04.c           ← I2C CO₂ 传感器驱动
│       ├── DHT11.c           ← 单总线温湿度驱动
│       ├── bmp280.c          ← 串口大气压模块解析
│       ├── csb.c             ← 串口超声波模块解析
│       ├── gnss.c            ← 串口 GNSS NMEA 解析
│       ├── EB21.c            ← 串口星闪模块 AT 指令
│       └── lcd.c             ← 串口 LCD 屏驱动
│
└── MDK-ARM/                  ← Keil MDK 工程文件与编译产物
```

**分层思想**（自底向上）：

```
第 5 层  应用层     main.c (while循环 / 中断回调 / 页面逻辑)
                          ↑
第 4 层  控制台层   console.c  (命令注册链表、命令分发)
                          ↑
第 3 层  模块/驱动层 BH1750.c  SCD04.c  DHT11.c  bmp280.c
                     csb.c  gnss.c  EB21.c  lcd.c
                          ↑
第 2 层  通信封装层  usr_uart.c (printf重定向)  +  HAL_UART_Receive_IT
                          ↑
第 1 层  工具层      queue.c (环形队列)  split_argv.c (分词)  list.h (链表)
                          ↑
第 0 层  HAL 库      stm32h7xx_hal_*.c (CubeMX/ST 提供)
```

## 1.4 完整执行流程（上电到运行）

```
┌─────────────────────────────────────────────────────────────┐
│ ① 上电 / 复位                                                 │
│    CPU 从 0x08000000 取栈顶指针 MSP，从 0x08000004 取 Reset   │
│    向量，跳到 startup_stm32h7xxxx.s 的 Reset_Handler         │
└──────────────────────────┬──────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ② 启动文件 startup_*.s                                        │
│    - 调用 SystemInit()  → 配置 FPU、复位 RCC 到默认态         │
│    - 调用 __main (Keil C库) → 拷贝 .data 到 SRAM，清零 .bss   │
│    - 跳转到 main()                                            │
└──────────────────────────┬──────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ③ HAL_Init()                                                 │
│    - 使能 Flash 预取指/ART                                    │
│    - 设置 NVIC 优先级分组 (4bit 抢占)                          │
│    - 配置 SysTick 为 1ms 中断 → HAL_Delay/HAL_GetTick 的基准  │
│    - 调用 HAL_MspInit()                                       │
└──────────────────────────┬──────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ④ SystemClock_Config()                                       │
│    - PWR 供电=LDO，电压等级=SCALE3（低功耗档）                 │
│    - 等 VOSRDY 标志置位（电压稳定）                            │
│    - 只开 HSI（内部 64MHz RC），PLL 关闭                       │
│    - SYSCLK = HSI = 64MHz, HCLK = 64MHz                      │
│    - APB1/2/3/4 = HCLK/2 = 32MHz                             │
│    - FLASH_LATENCY_1（64MHz 下 1 个等待周期）                  │
└──────────────────────────┬──────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ⑤ 外设初始化（CubeMX 生成的 MX_xxx_Init 系列）                 │
│    MX_GPIO_Init()   → LED/按键/EXTI/星闪控制脚                │
│    MX_TIM1_Init()   → 1MHz 微秒计数器                         │
│    MX_USART1_UART_Init() → 调试控制台 115200                  │
│    MX_USART3_UART_Init() → LCD 屏 115200                      │
│    MX_I2C3_Init()   → BH1750                                 │
│    MX_USART10_UART_Init() → BMP280 115200                    │
│    MX_I2C2_Init()   → SCD41                                  │
│    MX_TIM3_Init()   → PWM 蜂鸣器                              │
│    MX_UART4_Init()  → GNSS 115200                             │
│    MX_UART5_Init()  → 超声波 9600                             │
│    MX_UART7_Init()  → 星闪 115200                             │
│    MX_TIM2_Init()   → 1 秒节拍                                │
└──────────────────────────┬──────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ⑥ 用户初始化 (USER CODE BEGIN 2)                              │
│    1. 启动 TIM3 PWM，占空比设 0（蜂鸣器静音）                   │
│    2. SCD04_INIT()  → 填 I2C 句柄和地址（不发命令）             │
│    3. BH1750_INIT() → 上电+复位+连续高分辨率模式                │
│    4. 6 路串口全部启动"接收 1 字节中断"                         │
│    5. 启动 TIM2 中断（1 秒节拍开始跑）                          │
│    6. console_init() → 建命令链表 + 注册 4 个系统命令 + 打欢迎屏│
│    7. 注册 8 个传感器测试命令（"1"~"8"）                        │
│    8. get_sensor_data() → 延时3s，首次采集 CO₂/温湿度/光照      │
│    9. lcd_set_page0() → 屏上显示第 0 页                        │
└──────────────────────────┬──────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ⑦ while(1) 主循环 —— 三件事轮询                                │
│                                                              │
│   console_run()        ← 从环形队列取串口字符，拼命令、回车执行 │
│   key_function()       ← 检查按键标志，翻 LCD 页面             │
│   timer_collect_data() ← 检查软计数器，到期就采集传感器+刷屏    │
│                                                              │
└──────────────────────────┬──────────────────────────────────┘
                           ↓ (中断异步打断)
┌─────────────────────────────────────────────────────────────┐
│ ⑧ 中断服务                                                    │
│   TIM2 每 1s → HAL_TIM_PeriodElapsedCallback                 │
│                → 4 个软计数器各减 1                            │
│   UART Rx  → HAL_UART_RxCpltCallback                         │
│              → 按串口号分发到各自协议解析状态机                 │
│   EXTI     → HAL_GPIO_EXTI_Callback                          │
│              → 置按键标志 / 直接开关灯+PWM                     │
└─────────────────────────────────────────────────────────────┘
```

---

# 第二部分 · CubeMX 自动生成部分（简述）

## 2.1 SystemClock_Config —— 时钟树

```c
void SystemClock_Config(void)
{
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);            // H7 特有：选择 LDO 供电模式
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3); // 电压档 3（最低，省电，限制最高频率）
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}    // 死等内核电压稳定

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI; // 只用内部 HSI
    RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;         // HSI 不分频 = 64MHz
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;     // 【关键】完全不用 PLL！
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI; // 系统时钟直接来自 HSI
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;     // 不分频
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;       // HCLK = 64MHz
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;      // APB1 = 32MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;      // APB2 = 32MHz
    ...
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1);
}
```

> **为什么不用 PLL 跑到 480MHz？** 因为这是工厂测试固件，只做串口/I2C 低速通信，64MHz 完全够用；用 HSI 不需要外部晶振，即使晶振焊错/虚焊，板子也能启动并报错——这对**产线测试**至关重要。这是一个非常聪明的设计取舍。

**为传感器驱动提供的基础环境**：
- 64MHz 主频 → TIM1 配 PSC=63 恰好得到 1MHz，DHT11 微秒延时精确
- SysTick 1ms → `HAL_Delay()` 可用（SCD41 需要 5000ms 延时）

## 2.2 MX_GPIO_Init —— 引脚配置

作用：
1. 使能 GPIOA/B/C/D/E/F 时钟
2. 配置 `GREEN(PA15)/YELLOW(PC12)/RED(PD2)` 为**推挽输出**，初值低电平（灯灭）
3. 配置 `KEY1(PE15)/KEY2(PE14)/KEY3(PE13)` 为 **EXTI 中断模式**，共用 `EXTI15_10_IRQn`
4. 配置星闪控制脚 `XS_RESET(PC13)/XS_POWER_CL(PF8)` 为输出，`XS_BUSY(PE4)/XS_DOUT(PE5)/XS_POWER_IND(PE6)` 为输入
5. `HAL_NVIC_EnableIRQ(EXTI15_10_IRQn)` 开中断

## 2.3 MX_I2Cx_Init —— 两路 I2C

```c
hi2c2.Init.Timing = 0x00707CBB;   // I2C2 → SCD41 CO₂
hi2c3.Init.Timing = 0x00707CBB;   // I2C3 → BH1750 光照
hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; // 7位地址模式
hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;    // 允许从机时钟拉伸
```

`Timing = 0x00707CBB` 是 CubeMX 根据 APB 时钟算出来的**标准模式 100kHz** 时序寄存器值。它是 `I2C_TIMINGR` 寄存器，位域含义：

| 位域 | 名称 | 值 | 含义 |
|---|---|---|---|
| [31:28] | PRESC | 0x0 | 时钟预分频 |
| [23:20] | SCLDEL | 0x7 | 数据建立时间 |
| [19:16] | SDADEL | 0x0 | 数据保持时间 |
| [15:8] | SCLH | 0x7C | SCL 高电平周期 |
| [7:0] | SCLL | 0xBB | SCL 低电平周期 |

## 2.4 MX_USARTx_Init —— 6 路串口

全部配置为 `8N1`（8 数据位、无校验、1 停止位），`Mode = TX_RX`（收发都开）：

| 串口 | 波特率 | 接谁 | 在 main.c 里的作用 |
|---|---|---|---|
| USART1 | 115200 | PC 调试串口 | `printf` 输出 + 命令行输入 |
| USART3 | 115200 | LCD 串口屏 | 发 `SET_TXT`/`JUMP` 指令，收 `OK\r\n` |
| UART4 | 115200 | GNSS | 收 NMEA `$GNRMC` 报文 |
| UART5 | **9600** | 超声波 | 收 4 字节二进制帧 |
| UART7 | 115200 | EB21 星闪 | 发 `AT+MAC?` 收回应 |
| USART10 | 115200 | BMP280 模块 | 收 `Pressure ... ` ASCII 文本 |

## 2.5 MX_TIMx_Init —— 三个定时器

| 定时器 | PSC | ARR | 结果 | 用途 |
|---|---|---|---|---|
| TIM1 | 63 | 65535 | 1MHz 自由计数 | DHT11 微秒延时基准 |
| TIM2 | 6399 | 9999 | 1 秒中断 | 全局任务节拍 |
| TIM3 | 63 | 99 | 10kHz PWM | CH3 输出蜂鸣器，Pulse 初值 50 |

## 2.6 stm32h7xx_hal_msp.c / stm32h7xx_it.c

- **`hal_msp.c`**：HAL 库的"底层适配"回调。`HAL_UART_MspInit()` 在 `HAL_UART_Init()` 内部被自动调用，负责**使能该串口时钟 + 配置对应的 GPIO 复用功能(AF) + 使能 NVIC 中断**。
- **`stm32h7xx_it.c`**：真正的中断向量函数，如 `USART1_IRQHandler()` 里只有一句 `HAL_UART_IRQHandler(&huart1);`，HAL 处理完标志位后，回调到用户写的 `HAL_UART_RxCpltCallback()`。

---

# 第三部分 · BSP 核心代码逐文件逐行详解

> 按 **工具层 → 通信层 → 传感器层 → 模块层 → 应用层** 顺序讲解。

---

## 3.1 【工具层】`BSP/Inc/list.h` —— Linux 风格侵入式双向链表

### 文件定位

这是从 Linux 内核 `include/linux/list.h` 移植的**纯头文件**双向循环链表。整个 console 的命令注册机制完全建立在它之上。

### 核心思想：侵入式链表

普通链表是"链表节点里存数据指针"，侵入式链表反过来——**把链表节点嵌进你的数据结构里**：

```c
struct list_head {
    struct list_head *next, *prev;   // 只有两个指针，没有 data 字段
};

typedef struct cmd_item_ {
    char command[24];
    char help[200];
    console_cmd_func_t func;
    struct list_head list;    // ← 链表节点嵌在结构体里面
    unsigned char success;
    unsigned char fail;
} cmd_item_t;
```

**好处**：链表代码完全通用，不需要为每种数据类型重写；不需要额外 malloc 节点。

**难点**：拿到 `struct list_head *` 怎么反推出外层 `cmd_item_t *`？靠 `container_of` 宏：

```c
// offsetof(TYPE, MEMBER)：把 0 强转成 TYPE* 指针，取 MEMBER 的地址，
// 因为基址是 0，所以这个地址值就等于 MEMBER 在结构体内的字节偏移量
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

// container_of：已知成员指针 ptr，反推容器首地址
#define container_of(ptr, type, member) ({          \
    const typeof(((type *)0)->member) *__mptr = (ptr);  \
    (type *)((char *)__mptr - offsetof(type, member)); })
    // 原理：成员地址 - 成员偏移量 = 结构体首地址
```

### 关键宏

```c
// 初始化：空链表的 next 和 prev 都指向自己（循环链表）
#define INIT_LIST_HEAD(ptr) do { \
    (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

// 在 prev 和 next 之间插入 new
static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next)
{
    next->prev = new;    // 后继的 prev 指向新节点
    new->next = next;    // 新节点的 next 指向后继
    new->prev = prev;    // 新节点的 prev 指向前驱
    prev->next = new;    // 前驱的 next 指向新节点
}

// 尾插：插到 head 之前（因为是循环链表，head->prev 就是最后一个节点）
static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
    __list_add(new, head->prev, head);
}

// 安全遍历（遍历中可以删除当前节点，因为提前保存了 n）
#define list_for_each_entry_safe(pos, n, head, member)         \
    for (pos = list_entry((head)->next, typeof(*pos), member), \
         n = list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head);                                \
         pos = n, n = list_entry(n->member.next, typeof(*n), member))
```

**在本项目中的作用**：`console.c` 用 `cmd_list` 作为链表头，每注册一个命令就 `list_add_tail` 一个 `cmd_item_t`；每次执行命令就用 `list_for_each_entry_safe` 遍历比对命令字符串。

---

## 3.2 【工具层】`BSP/Src/queue.c` + `queue.h` —— 环形缓冲队列

### 文件定位

**串口接收与主循环之间的解耦缓冲区**。串口中断（高优先级、必须极快返回）只负责把收到的字节丢进队列；主循环（低优先级、可以慢慢处理）从队列里取出来解析。这是嵌入式最经典的**生产者-消费者**模式。

### 完整源码逐行注释

**`queue.h`：**

```c
#ifndef __QUEUE_H
#define __QUEUE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define BUFFER_SIZE 128          // 环形缓冲区容量 128 字节
                                 // 注意：实际最多只能存 127 个，见下面"满"的判断

bool queue_in(char *data);       // 入队（生产者：串口中断调用）
bool queue_out(char *data);      // 出队（消费者：主循环 console_run 调用）

#endif /* __QUEUE_H */
```

**`queue.c`：**

```c
#include "queue.h"

// 环形缓冲区的结构体
__packed typedef struct     // __packed：告诉编译器不要做字节对齐填充，节省 RAM
{
    char buffer[BUFFER_SIZE];  // 实际存数据的数组
    int head;                  // 写指针（生产者移动）：下一个要写入的位置
    int tail;                  // 读指针（消费者移动）：下一个要读出的位置
} CircularBuffer;

static CircularBuffer cirbuffer = {0};  // static：文件私有，外部只能通过 in/out 函数访问
                                        // = {0} 让 head/tail 初值都为 0（空队列）

// 初始化缓冲区
void initBuffer(void)
{
    cirbuffer.head = 0;
    cirbuffer.tail = 0;
    // head == tail 表示队列为空
}

// 写入缓冲区（生产者）
bool queue_in(char *data)
{
    // 【核心判满逻辑】
    // 如果 head 再往前走一格就撞上 tail，说明队列已满
    // 为什么要牺牲一个格子？因为 head==tail 已经被定义为"空"，
    // 如果让 head 追平 tail 表示"满"，就无法区分空和满了。
    // 所以留一个空格作为哨兵，最多存 BUFFER_SIZE-1 = 127 字节
    if (((cirbuffer.head + 1) % BUFFER_SIZE) == cirbuffer.tail)
    {
        return false;   // 满了，丢弃这个字节（数据溢出）
    }

    cirbuffer.buffer[cirbuffer.head] = *data;              // 把数据写到 head 位置
    cirbuffer.head = (cirbuffer.head + 1) % BUFFER_SIZE;   // head 前进一格
                                                           // % BUFFER_SIZE 实现"绕回"
                                                           // 127 → 128%128 = 0，回到开头
    return true;
}

// 从缓冲区读取数据（消费者）
bool queue_out(char *data)
{
    // 【判空逻辑】读写指针重合 = 空
    if (cirbuffer.head == cirbuffer.tail)
    {
        return false; // 缓冲区为空，没数据可读
    }

    *data = cirbuffer.buffer[cirbuffer.tail];              // 取出 tail 位置的数据
    cirbuffer.tail = (cirbuffer.tail + 1) % BUFFER_SIZE;   // tail 前进一格并绕回

    return true;
}
```

### 环形队列工作原理图解

```
初始（空）：head=tail=0
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │   │   │   │   │   │   │   │   │
  └───┴───┴───┴───┴───┴───┴───┴───┘
    ↑
   h,t

写入 'a','b','c' 后：head=3, tail=0
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │ a │ b │ c │   │   │   │   │   │
  └───┴───┴───┴───┴───┴───┴───┴───┘
    ↑           ↑
    t           h

读出 'a' 后：head=3, tail=1
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │ - │ b │ c │   │   │   │   │   │
  └───┴───┴───┴───┴───┴───┴───┴───┘
        ↑       ↑
        t       h

head 绕回后（head=7 再写一次 → head=0）：
  两个指针在环上追逐，永不"越界"
```

> **⚠️ 本项目的一个隐患**：`initBuffer()` 定义了但从未在 `main()` 中调用。好在 `static CircularBuffer cirbuffer = {0}` 让编译器把它放在 `.bss` 段，启动文件已经清零，所以 head/tail 天然是 0，功能正常。

---

## 3.3 【工具层】`BSP/Src/split_argv.c` —— 命令行参数解析状态机

### 文件定位

来源于 **ESP-IDF 的 console 组件**（Apache-2.0 协议，见文件头）。作用是把一行命令字符串 `set -i 192.168.1.66 -o "hello world"` 拆分成 `argv[]` 数组，**支持引号和转义字符**。

它是一个**原地（in-place）解析器**——不额外申请内存，直接在原字符串上把空格改成 `\0`，然后让 `argv[i]` 指向各段起始位置。

### 完整源码逐行注释

```c
/*
 * SPDX-FileCopyrightText: 2016-2021 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define SS_FLAG_ESCAPE 0x8      // 转义标志位（bit3）
                                // 用"或"的方式叠加到状态上，实现状态的正交组合

#define MAX_ARGS 10             // 最大参数个数（本文件内未实际使用，console.c 里用的是 6）

// 解析状态机的 5 个状态
typedef enum
{
    /* 正在跳过参数之间的空格 */
    SS_SPACE = 0x0,
    /* 正在解析一个"未加引号"的参数 */
    SS_ARG = 0x1,
    /* 正在解析一个"加了引号"的参数 */
    SS_QUOTED_ARG = 0x2,
    /* 在未加引号的参数中遇到了反斜杠，等待下一个字符 */
    SS_ARG_ESCAPED = SS_ARG | SS_FLAG_ESCAPE,             // = 0x1 | 0x8 = 0x9
    /* 在加引号的参数中遇到了反斜杠，等待下一个字符 */
    SS_QUOTED_ARG_ESCAPED = SS_QUOTED_ARG | SS_FLAG_ESCAPE, // = 0x2 | 0x8 = 0xA
} split_state_t;

/* 辅助宏：一个参数解析完毕时调用 */
#define END_ARG()                      \
    do                                 \
    {                                  \
        char_out = 0;                  /* 输出一个 '\0'，给刚才那段字符串收尾 */ \
        argv[argc++] = next_arg_start; /* 把这段的起始地址存进 argv，参数计数+1 */ \
        state = SS_SPACE;              /* 回到"跳空格"状态，准备解析下一个参数 */ \
    } while (0)

size_t console_split_argv(char *line, char **argv, size_t argv_size)
{
    const int QUOTE = '"';        // 引号字符
    const int ESCAPE = '\\';      // 反斜杠（C 里 '\\' 表示一个反斜杠）
    const int SPACE = ' ';        // 空格
    split_state_t state = SS_SPACE;   // 初始状态：准备跳过前导空格
    size_t argc = 0;                  // 已解析出的参数个数
    char *next_arg_start = line;      // 当前正在解析的参数的起始地址
    char *out_ptr = line;             // 【写指针】原地改写用，指向下一个要写入的位置

    // 【双指针原地压缩】in_ptr 只读向前扫描，out_ptr 写入结果
    // out_ptr 永远不会超过 in_ptr，所以原地改写是安全的
    for (char *in_ptr = line; argc < argv_size - 1; ++in_ptr)
    {
        int char_in = (unsigned char)*in_ptr;  // 读入当前字符
                                               // 转 unsigned char 避免中文等高位字符被当成负数
        if (char_in == 0)
        {
            break;      // 遇到字符串结束符，退出循环
        }
        int char_out = -1;   // -1 表示"本次不输出任何字符"（比如空格被跳过）

        switch (state)
        {
        // ============ 状态1：正在跳过空格 ============
        case SS_SPACE:
            if (char_in == SPACE)
            {
                /* 连续空格，直接跳过，什么都不做 */
            }
            else if (char_in == QUOTE)
            {
                next_arg_start = out_ptr;  // 记住这个参数从哪开始（引号本身不算内容）
                state = SS_QUOTED_ARG;     // 进入"引号参数"状态
            }
            else if (char_in == ESCAPE)
            {
                next_arg_start = out_ptr;
                state = SS_ARG_ESCAPED;    // 进入"转义"状态，下个字符原样输出
            }
            else
            {
                next_arg_start = out_ptr;  // 普通字符，参数开始
                state = SS_ARG;
                char_out = char_in;        // 这个字符要输出
            }
            break;

        // ============ 状态2：正在解析引号内的参数 ============
        case SS_QUOTED_ARG:
            if (char_in == QUOTE)
            {
                END_ARG();          // 遇到闭合引号 → 参数结束
            }
            else if (char_in == ESCAPE)
            {
                state = SS_QUOTED_ARG_ESCAPED;  // 引号内的转义
            }
            else
            {
                char_out = char_in; // 引号内的空格也会被原样保留！这就是引号的意义
            }
            break;

        // ============ 状态3/4：转义状态（两种情况处理逻辑相同） ============
        case SS_ARG_ESCAPED:
        case SS_QUOTED_ARG_ESCAPED:
            if (char_in == ESCAPE || char_in == QUOTE || char_in == SPACE)
            {
                char_out = char_in;  // \\ → \    \" → "    \  → 空格
                                     // 这三种转义有效，输出原字符
            }
            else
            {
                /* 无法识别的转义序列，直接丢弃这个字符 */
            }
            // 【位运算清除转义标志，回到之前的状态】
            // SS_ARG_ESCAPED(0x9) & ~0x8 = 0x1 = SS_ARG
            // SS_QUOTED_ARG_ESCAPED(0xA) & ~0x8 = 0x2 = SS_QUOTED_ARG
            // 这就是为什么用位标志设计状态——一行代码完成"返回上一状态"
            state = (split_state_t)(state & (~SS_FLAG_ESCAPE));
            break;

        // ============ 状态5：正在解析普通（无引号）参数 ============
        case SS_ARG:
            if (char_in == SPACE)
            {
                END_ARG();          // 遇到空格 → 参数结束
            }
            else if (char_in == ESCAPE)
            {
                state = SS_ARG_ESCAPED;
            }
            else
            {
                char_out = char_in;
            }
            break;
        }

        /* 需要输出字符吗？ */
        if (char_out >= 0)          // >= 0 说明本轮有字符要写（0 就是 '\0' 分隔符）
        {
            *out_ptr = char_out;    // 写到 out_ptr 指向的位置
            ++out_ptr;              // 写指针前进
        }
    }

    /* 确保最后一个参数以 '\0' 结尾 */
    *out_ptr = 0;

    /* 收尾：如果循环结束时还在解析参数中（没遇到空格就到字符串末尾了） */
    if (state != SS_SPACE && argc < argv_size - 1)
    {
        argv[argc++] = next_arg_start;   // 把最后一个参数也记录进去
    }

    /* 给 argv 数组末尾放一个 NULL，符合 C 的 main(argc, argv) 惯例 */
    argv[argc] = NULL;

    return argc;    // 返回参数个数
}
```

### 解析过程实例演示

输入：`set -i 192.168.1.66`

```
原始内存： s  e  t  ' '  -  i  ' '  1  9  2  .  1 ...
in_ptr 扫描 →
out_ptr 写入：

步骤  in    state         动作                out_ptr写入
 1    's'   SPACE→ARG     记 next_arg_start   's'
 2    'e'   ARG           输出                'e'
 3    't'   ARG           输出                't'
 4    ' '   ARG→SPACE     END_ARG()           '\0'  argv[0]="set"
 5    '-'   SPACE→ARG     记 next_arg_start   '-'
 6    'i'   ARG           输出                'i'
 7    ' '   ARG→SPACE     END_ARG()           '\0'  argv[1]="-i"
 8..  '1'.. ARG           输出                "192.168.1.66"
 结束        state==ARG    argv[2]=起始地址

结果内存：s e t \0 - i \0 1 9 2 . 1 6 8 . 1 . 6 6 \0
          ↑        ↑      ↑
        argv[0]  argv[1] argv[2]
返回 argc = 3
```

---

## 3.4 【通信层】`BSP/Src/usr_uart.c` + `usr_uart.h` —— printf 重定向

### 文件定位

**整个项目所有 `printf` 输出的最底层出口**。原理是重写 C 标准库的 `fputc()` 函数——Keil MDK 的 microlib/标准库中，`printf` 最终会逐字符调用 `fputc()`，我们把它重定向到 USART1。

### 完整源码逐行注释

**`usr_uart.h`：**

```c
#ifndef __USR_UART_H
#define __USR_UART_H

#include <string.h>
#include <stdio.h>
#include "main.h"        // 需要 UART_HandleTypeDef 类型定义

// 串口发送封装函数声明
extern HAL_StatusTypeDef UART_SenddBuf(UART_HandleTypeDef *huart,
                                       const uint8_t *pData,
                                       uint16_t Size,
                                       uint32_t Timeou);

// 【关键全局变量】USART1 单字节接收缓存
// HAL_UART_Receive_IT(&huart1, &dataRcvd, 1) 会把收到的字节存到这里
extern uint8_t dataRcvd;

#endif
```

**`usr_uart.c`：**

```c
#include "usr_uart.h"

/* USER CODE BEGIN PV */
#define RXBUFFERSIZE 256      // 定义了但本文件未使用（遗留代码）

uint8_t dataRcvd;             // USART1 接收单字节缓存
                              // 中断回调里 queue_in(&dataRcvd) 把它送进环形队列

extern UART_HandleTypeDef huart1;   // 引用 usart.c 里定义的 USART1 句柄

// 串口发送封装：本质就是包了一层 HAL_UART_Transmit
HAL_StatusTypeDef UART_SenddBuf(UART_HandleTypeDef *huart,
                                const uint8_t *pData,
                                uint16_t Size,
                                uint32_t Timeou)
{
    // 【注意】这里有个"巧合式"写法：第4个参数超时值传的是 Size 而不是 Timeou
    // 也就是说：发 1 字节超时 1ms，发 100 字节超时 100ms
    // 恰好和"每字节需要一定时间"的直觉吻合，所以实际能工作
    // 但严格来说这是个笔误——形参 Timeou 被忽略了
    return (HAL_UART_Transmit(huart, pData, Size, Size));
}

// 【核心】重定向 printf 到 USART1
int fputc(int ch, FILE *f)
{
    UART_SenddBuf(&huart1, (uint8_t *)&ch, 1, 5);
    // 把 int ch 的地址强转为 uint8_t*，因为小端序，低字节就是我们要的字符
    // 每次只发 1 个字节，超时 1ms（因为 Size=1 被当成了超时）

    // 【隐患】函数声明了返回 int 但没有 return 语句！
    // 标准做法应该是 return ch;
    // 实际运行时返回 R0 寄存器的残留值，printf 一般不检查返回值，所以不影响功能
}
```

### printf 调用链

```
你的代码:  printf("lx: %d\r\n", 100);
              ↓
C 标准库:  格式化解析 → 逐字符输出
              ↓
          fputc('l', stdout)
          fputc('x', stdout)
          ...
              ↓
我们重写:  UART_SenddBuf(&huart1, &ch, 1, 5)
              ↓
HAL 库:    HAL_UART_Transmit(&huart1, &ch, 1, 1)
              ↓
硬件:      轮询等 TXE 标志 → 写 USART1->TDR 寄存器 → 移位输出
```

> **性能提示**：这是**阻塞式逐字节发送**。115200 波特率下发 1 字节约 87μs，打印一行 50 字符要 4.3ms。所以在中断里千万不要 printf。项目中 `bh7150_read()` 等命令函数里大量 printf，但它们运行在主循环里，可以接受。

---

## 3.5 【传感器层·I2C】`BSP/Src/BH1750.c` —— 数字光照强度传感器

### 3.5.1 器件原理

**BH1750FVI** 是 ROHM 公司的 16 位数字环境光传感器：
- 内置光电二极管 + 运放 + 16bit ADC
- I2C 接口，测量范围 **1 ~ 65535 lx**
- 精度典型 ±20%，分辨率 1 lx（高分辨率模式）
- **地址由 ADDR 引脚决定**：ADDR 接地 → 0x23（7位）；ADDR 接 VCC → 0x5C（7位）

### 3.5.2 完整源码逐行注释

```c
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "main.h"
#include "console.h"
#include "BH1750.h"

#define READ_CMD "-r"        // 遗留宏，本文件未使用

// ============ I2C 从机地址定义（注意：这里是 8 位地址，含读写位） ============
// HAL 库的 I2C 函数要求传入"左移一位后的 8 位地址"
#define BH1750_NO_GROUND_ADDR_WRITE (0xB9 + 0)   // ADDR 悬空/接高：0xB8 写 (0x5C<<1 = 0xB8)
#define BH1750_NO_GROUND_ADDR_READ  (0xB9 + 1)   // 读 0xBA
#define BH1750_GROUND_ADDR_WRITE    (0x46 + 0)   // ADDR 接地：0x46 写 (0x23<<1 = 0x46)
#define BH1750_GROUND_ADDR_READ     (0x46 + 1)   // 读 0x47

// 【注意】上面 NO_GROUND 那两个宏的数值有笔误：
//   标准应该是 WRITE=0xB8, READ=0xB9
//   这里写成了 WRITE=0xB9, READ=0xBA
//   但本项目实际用的是 GROUND 分支（ADDR 接地，0x46/0x47 是正确的），所以不影响

// ============ BH1750 指令集（数据手册 Instruction Set Architecture） ============
#define CMD_POWER_DOWN      0x00  // 掉电模式：0000_0000，测量停止，功耗最低
#define CMD_POWER_ON        0x01  // 上电模式：0000_0001，等待测量指令
#define CMD_RESET           0x03  // 复位：0000_0011，清零数据寄存器（掉电模式下无效）
#define CMD_H_RES_MODE      0x10  // 连续高分辨率模式：0001_0000
                                  //   分辨率 1lx，测量时间约 120ms
#define CMD_H_RES_MODE2     0x11  // 连续高分辨率模式2：分辨率 0.5lx，约 120ms
#define CMD_L_RES_MODE      0x13  // 连续低分辨率模式：分辨率 4lx，约 16ms
#define CMD_ONE_H_RES_MODE  0x20  // 单次高分辨率模式（测完自动进 POWER_DOWN）
#define CMD_ONE_H_RES_MODE2 0x21  // 单次高分辨率模式2
#define CMD_ONE_L_RES_MODE  0x23  // 单次低分辨率模式
#define CMD_CNG_TIME_HIGH   0x30  // 改变测量时间-高字节：01000_MT[7:5]，3 个 LSB 位
#define CMD_CNG_TIME_LOW    0x60  // 改变测量时间-低字节：011_MT[4:0]，5 个 LSB 位

uint16_t lx_Data = -1;   // 【全局变量】光照值，单位 lx
                         // 初值 -1 转成 uint16_t 就是 65535，表示"还没测过"
                         // main.c 里 lcd_set_page0() 会读它显示到屏上

// ============ 设备对象结构体（面向对象的封装思想） ============
typedef struct BH1750_device
{
    char name[10];                  // 设备名，方便调试打印

    I2C_HandleTypeDef *i2c_handle;  // 挂在哪路 I2C 上（本项目是 hi2c3）
    uint8_t address_r;              // 读地址
    uint8_t address_w;              // 写地址

    uint16_t value;                 // 最近一次测量的光照值 (lx)

    uint8_t buffer[2];              // 接收缓冲：BH1750 一次返回 2 字节

} BH1750_device_t;

extern I2C_HandleTypeDef hi2c3;             // I2C3 句柄（i2c.c 中定义）
static BH1750_device_t bh7150_dev = {0};    // 本模块唯一的设备实例

// ============ 函数1：发送单字节指令 ============
static HAL_StatusTypeDef BH1750_send_command(BH1750_device_t *dev, uint8_t cmd)
{
    // TODO hal checks
    if (HAL_I2C_Master_Transmit(
            dev->i2c_handle, // I2C 句柄
            dev->address_w,  // 从机写地址 0x46
            &cmd,            // 要发送的指令字节的地址
            1,               // 只发 1 个字节（BH1750 的指令都是单字节）
            10               // 超时 10ms
            ) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}
// 【I2C 总线时序】这一句 HAL 调用在总线上产生：
//   START | 0x46(写地址) | ACK | cmd | ACK | STOP
```

**函数2：初始化设备结构体**

```c
static void BH1750_init_dev_struct(BH1750_device_t *dev, I2C_HandleTypeDef *i2c_handle,
                                   char *name, uint8_t addr_grounded)
{
    if (addr_grounded)      // ADDR 引脚接地
    {
        dev->address_r = BH1750_GROUND_ADDR_READ;   // 0x47
        dev->address_w = BH1750_GROUND_ADDR_WRITE;  // 0x46
    }
    else                    // ADDR 引脚接高电平
    {
        dev->address_r = BH1750_NO_GROUND_ADDR_READ;
        dev->address_w = BH1750_NO_GROUND_ADDR_WRITE;
    }
    dev->i2c_handle = i2c_handle;   // 保存 I2C 句柄指针

    strcpy(dev->name, name);        // 拷贝设备名
}
```

**函数3：硬件初始化（3 步走，非常经典）**

```c
static HAL_StatusTypeDef BH1750_init_dev(BH1750_device_t *dev)
{
    BH1750_send_command(dev, CMD_POWER_ON);    // ① 上电：从掉电模式唤醒，
                                               //    芯片内部振荡器启动，进入等待指令状态
    BH1750_send_command(dev, CMD_RESET);       // ② 复位：清空 16 位数据寄存器
                                               //    【重要】必须在 POWER_ON 之后发，
                                               //    因为掉电模式下 RESET 指令会被忽略
    BH1750_send_command(dev, CMD_H_RES_MODE);  // ③ 启动连续高分辨率测量模式
                                               //    从此芯片每约 120ms 自动测一次，
                                               //    结果自动更新到数据寄存器，
                                               //    主机随时可以直接读，不用每次发指令

    return HAL_OK;
}
```

> **为什么选"连续模式"而不是"单次模式"？** 单次模式每次读之前都要发指令 + 等 120ms，主机会阻塞。连续模式芯片自己一直在测，主机想读就读，**读取延时接近 0**，非常适合本项目"3 秒采一次"的场景。

**函数4：读取光照值（核心数据转换）**

```c
static HAL_StatusTypeDef BH1750_get_lumen(BH1750_device_t *dev)
{
    if (HAL_I2C_Master_Receive(dev->i2c_handle,
                               dev->address_r,   // 读地址 0x47
                               dev->buffer,      // 存到 buffer[0], buffer[1]
                               2,                // 读 2 个字节
                               10) != HAL_OK)    // 超时 10ms
        return HAL_ERROR;
    // I2C 总线时序：START | 0x47(读地址) | ACK | 收到高字节 | ACK | 收到低字节 | NACK | STOP

    /* 按照数据手册计算 */
    dev->value = dev->buffer[0];                        // 取高 8 位
    dev->value = (dev->value << 8) | dev->buffer[1];    // 左移 8 位，或上低 8 位
                                                        // 拼成 16 位原始计数值
                                                        // 例：buffer[0]=0x02, buffer[1]=0xD0
                                                        //     → 0x02D0 = 720

    // TODO check float stuff
    dev->value /= 1.2;   // 【核心转换公式】
    // ================== 公式推导 ==================
    // BH1750 数据手册第 11 页明确给出：
    //     照度[lx] = 传感器原始输出值 / 1.2
    //
    // 这个 1.2 是芯片的"测量精度系数（Measurement Accuracy）"，
    // 在默认测量时间（MTreg = 69）和 H-Resolution Mode 下的转换系数。
    //
    // 完整通式：
    //     lx = 原始值 / 1.2 × (69 / MTreg) × (1 or 2)
    //          └─精度系数  └─测量时间修正   └─模式2要除2
    //
    // 本项目用默认 MTreg=69 且是 H-Res Mode（非 Mode2），
    // 所以后两项都是 1，简化成 lx = raw / 1.2
    //
    // 例：raw = 720 → 720 / 1.2 = 600 lx
    // ==============================================
    //
    // 【注意】dev->value 是 uint16_t，除以浮点 1.2 时：
    //   C 语言会先把 value 提升为 double，做浮点除法，再截断回整数
    //   720 / 1.2 = 600.0 → 600（正好）
    //   721 / 1.2 = 600.83 → 600（截断，损失小数）
    //   这会引入最多 1lx 的量化误差，对本应用完全可接受

    return HAL_OK;
}
```

**函数5：对外的采集接口**

```c
uint16_t bh7150_get_data(void)
{
    BH1750_get_lumen(&bh7150_dev);   // 读取并转换
    lx_Data = bh7150_dev.value;      // 同步到全局变量，供 LCD 显示用
    return bh7150_dev.value;
}
```

**函数6：控制台测试命令回调**

```c
static int bh7150_read(int argc, char **argv)
{
    /* read bh7150 data ，please wait 2000ms */
    PRINTF("\r\n---------光照传感器测试--------------- \r\n");
    bh7150_dev.value = -1;              // 先置为"无效值" 65535
                                        // 目的：如果 I2C 读失败，value 保持 65535
    BH1750_get_lumen(&bh7150_dev);      // 尝试读取
    lx_Data = bh7150_dev.value;
    PRINTF("\r\n* lx: %ld  \r\n", bh7150_dev.value);   // 打印结果

    if (bh7150_dev.value == -1)         // 判断是否还是无效值
    {                                   // 【注意】uint16_t 和 -1 比较，
                                        // 编译器会把 -1 提升为 int 0xFFFFFFFF，
                                        // 而 value 提升为 0x0000FFFF，两者不相等！
                                        // 所以这个判断实际上永远为假 —— 是个 BUG
                                        // 正确写法应该是 == 0xFFFF 或 == (uint16_t)-1
        PRINTF("\r\n -----------BH1750 FAIL------------------- \r\n");
        return 0;      // 返回 0 = 测试失败
    }
    PRINTF("\r\n -----------BH1750 PASS------------------- \r\n");
    return 1;          // 返回 1 = 测试通过
}
```

**函数7/8：命令注册与模块初始化**

```c
void BH7150_cmd_register(void)
{
    // 定义一个命令项：在终端输入 "1" 就执行 bh7150_read
    const cmd_item_t bh7150cmd = {
        .command = "1",                  // 命令字符串
        .help = " 光照强度传感器 ",       // 帮助文本
        .func = &bh7150_read,            // 回调函数指针
        .success = 0,                    // 成功计数器清零
        .fail = 0,                       // 失败计数器清零
    };
    // 【注意】这个结构体是局部变量（栈上），但 console_cmd_register 内部会
    //         malloc 一份新内存并 strcpy 拷贝内容，所以函数返回后栈销毁也没关系
    console_cmd_register((cmd_item_t *)&bh7150cmd);
}

void BH1750_INIT(void)
{
    // 绑定到 I2C3，ADDR 引脚接地（第4个参数 true）
    BH1750_init_dev_struct(&bh7150_dev, &hi2c3, "BH1750", true);
    BH1750_init_dev(&bh7150_dev);   // 发 3 条初始化指令
}
```

---

## 3.6 【传感器层·I2C】`BSP/Src/SCD04.c` —— SCD41 二氧化碳传感器

### 3.6.1 器件原理

**Sensirion SCD41** 是基于 **光声法（Photoacoustic）** 的 CO₂ 传感器：
- 测量范围 400 ~ 5000 ppm，精度 ±(40 ppm + 5%读数)
- 同时输出 CO₂、温度、湿度三个量
- I2C 地址固定 **0x62（7位）**
- **每条指令是 2 字节（16 位）**，返回数据每 2 字节后跟 1 字节 **CRC-8 校验**

### 3.6.2 完整源码逐行注释

```c
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "main.h"
#include "console.h"
#include "SCD04.h"

#define READ_CMD   "-r"     // 遗留宏，未使用
#define SERIAL_CMD "-s"
#define STATUS_CMD "-st"
#define SINGLE_CMD "-sg"

// ============ I2C 地址 ============
#define SCD4x_ADDR_WRITE (0x62 << 1)         // 0xC4：7位地址 0x62 左移1位，最低位0=写
#define SCD4x_ADDR_READ  (0x62 << 1 | 0x1)   // 0xC5：最低位1=读

// ============ SCD4x 指令集（全部是 16 位命令码，来自官方数据手册） ============

// --- 基本命令 ---
#define SCD4x_COMMAND_START_PERIODIC_MEASUREMENT 0x21b1  // 启动周期测量（每5秒一次）
#define SCD4x_COMMAND_READ_MEASUREMENT           0xec05  // 读测量结果，执行时间 1ms
#define SCD4x_COMMAND_STOP_PERIODIC_MEASUREMENT  0x3f86  // 停止周期测量，执行时间 500ms

// --- 片上信号补偿（校准相关） ---
#define SCD4x_COMMAND_SET_TEMPERATURE_OFFSET 0x241d // 设置温度偏移（补偿自发热），1ms
#define SCD4x_COMMAND_GET_TEMPERATURE_OFFSET 0x2318 // 读温度偏移，1ms
#define SCD4x_COMMAND_SET_SENSOR_ALTITUDE    0x2427 // 设置海拔高度（气压影响CO₂读数），1ms
#define SCD4x_COMMAND_GET_SENSOR_ALTITUDE    0x2322 // 读海拔设置，1ms
#define SCD4x_COMMAND_SET_AMBIENT_PRESSURE   0xe000 // 设置环境气压（比海拔更精确的补偿），1ms

// --- 现场校准 ---
#define SCD4x_COMMAND_PERFORM_FORCED_CALIBRATION             0x362f // 强制重校准(FRC)，400ms
#define SCD4x_COMMAND_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED 0x2416 // 开/关自动自校准(ASC)，1ms
#define SCD4x_COMMAND_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED 0x2313 // 读 ASC 状态，1ms

// --- 低功耗 ---
#define SCD4x_COMMAND_START_LOW_POWER_PERIODIC_MEASUREMENT 0x21ac // 低功耗周期测量（30秒一次）
#define SCD4x_COMMAND_GET_DATA_READY_STATUS                0xe4b8 // 查询数据是否就绪，1ms

// --- 高级功能 ---
#define SCD4x_COMMAND_PERSIST_SETTINGS      0x3615 // 把设置写入 EEPROM 永久保存，800ms
#define SCD4x_COMMAND_GET_SERIAL_NUMBER     0x3682 // 读 48 位序列号，1ms
#define SCD4x_COMMAND_PERFORM_SELF_TEST     0x3639 // 自检，10000ms(!)
#define SCD4x_COMMAND_PERFORM_FACTORY_RESET 0x3632 // 恢复出厂设置，1200ms
#define SCD4x_COMMAND_REINIT                0x3646 // 重新初始化（从EEPROM重载配置），20ms

// --- 单次测量（仅 SCD41 支持，SCD40 没有） ---
#define SCD4x_COMMAND_MEASURE_SINGLE_SHOT          0x219d // 单次测CO₂+温湿度，执行时间 5000ms(!)
#define SCD4x_COMMAND_MEASURE_SINGLE_SHOT_RHT_ONLY 0x2196 // 只测温湿度，50ms

uint16_t data_co2 = -1;    // 【全局变量】CO₂ 浓度，单位 ppm
                           // 初值 65535 表示"未测量"

// ============ 设备结构体 ============
typedef struct _device
{
    char name[10];
    I2C_HandleTypeDef *i2c_handle;
    uint8_t address_r;
    uint8_t address_w;
    uint8_t buffer[20];    // 接收缓冲，最大需要 9 字节（3组数据×3字节）
} device_t;

extern I2C_HandleTypeDef hi2c2;    // SCD41 挂在 I2C2
static device_t scd04_dev;
```

**函数1：底层 I2C 发送**

```c
static HAL_StatusTypeDef sensor_send_command(device_t *dev, uint8_t *cmd, uint16_t size)
{
    if (HAL_I2C_Master_Transmit(
            dev->i2c_handle,
            dev->address_w,   // 0xC4
            cmd,              // 命令字节数组
            size,             // 字节数（SCD4x 命令都是 2 字节）
            10                // 超时 10ms
            ) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}
```

**函数2：初始化设备结构体**

```c
static void SCD04_init_dev_struct(device_t *dev, I2C_HandleTypeDef *i2c_handle,
                                  char *name, uint8_t addr_grounded)
{
    // 【注意】addr_grounded 参数完全没用到 —— SCD4x 地址是硬件固定的 0x62，不可配置
    dev->address_r = SCD4x_ADDR_READ;    // 0xC5
    dev->address_w = SCD4x_ADDR_WRITE;   // 0xC4
    dev->i2c_handle = i2c_handle;

    strcpy(dev->name, name);
}
```

**函数3：底层 I2C 接收**

```c
static HAL_StatusTypeDef sensor_recv(device_t *dev, uint16_t size)
{
    if (HAL_I2C_Master_Receive(dev->i2c_handle,
                               dev->address_r,   // 0xC5
                               dev->buffer,      // 存入设备结构体的 buffer
                               size,             // 要读的字节数
                               10) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}
```

**函数4：发送 16 位命令（大端序转换）**

```c
static HAL_StatusTypeDef scd04_send_command(device_t *dev, uint16_t cmd, uint16_t size)
{
    uint8_t data[10] = {0};
    data[0] = cmd >> 8;        // 【高字节在前 = 大端序 Big-Endian】
                               // 例：cmd = 0x219D
                               //     data[0] = 0x219D >> 8 = 0x21
    data[1] = (uint8_t)cmd;    //     data[1] = (uint8_t)0x219D = 0x9D（截断保留低8位）
                               // I2C 总线上先发 0x21 再发 0x9D
                               // 【为什么是大端？】Sensirion 所有传感器协议都用大端（MSB first）

    if (sensor_send_command(dev, data, size))   // size 传的是 2
    {
        printf("\r\n CMD  0x%02x error.\r\n", cmd);
        return HAL_ERROR;
    }

    return HAL_OK;
}
```

**函数5：读取序列号（用于"探测传感器是否存在"）**

```c
static HAL_StatusTypeDef scd94_get_serial(device_t *dev)
{
    scd04_send_command(dev, SCD4x_COMMAND_GET_SERIAL_NUMBER, 2);   // 发 0x3682

    if (sensor_recv(dev, 9) != HAL_OK)    // 读 9 字节
    {
        return HAL_ERROR;   // 读失败 → 传感器不存在或接线错误
    }
    // ============ 9 字节数据格式（Sensirion 标准） ============
    //  byte[0] byte[1] byte[2] | byte[3] byte[4] byte[5] | byte[6] byte[7] byte[8]
    //  word0_H word0_L  CRC0   | word1_H word1_L  CRC1   | word2_H word2_L  CRC2
    //  └── 序列号第1个16位字 ──┘ └── 第2个16位字 ──────┘ └── 第3个16位字 ────┘
    //  三个 16 位字拼起来 = 48 位唯一序列号
    //  每 2 字节数据后跟 1 字节 CRC-8 校验（多项式 0x31，初值 0xFF）

    PRINTF("\r\n serial namber:");
    /* 按照数据手册计算 */
    for (char i = 1; i <= 9; i++)         // i 从 1 到 9（1-based 计数）
    {
        if (i % 3 == 0)                   // 每第 3 个就是 CRC 字节
        {
            continue;                     // 跳过 CRC，不打印
                                          // i=3 → buffer[2]=CRC0，跳过
                                          // i=6 → buffer[5]=CRC1，跳过
                                          // i=9 → buffer[8]=CRC2，跳过
        }
        PRINTF("%02x ", dev->buffer[i - 1]);   // 打印数据字节（i-1 转回 0-based 下标）
    }
    PRINTF("\r\n");
    // 最终打印 6 个字节：buffer[0][1][3][4][6][7] = 48 位序列号
    // 【注意】本项目只跳过了 CRC，并没有真的去校验 CRC。
    //         严格的实现应该用 CRC-8(0x31) 算法验证数据完整性。

    memset(dev->buffer, 0, 9);    // 清空缓冲，避免残留数据影响下次读取
    return HAL_OK;
}
```

**函数6/7：控制台测试命令**

```c
static int scd04_test(int argc, char **argv)
{
    PRINTF("\r\n---------二氧化碳传感器测试--------------- \r\n");

    // 【测试策略】不测 CO₂ 数值（因为要等 5 秒太慢），
    //             改为读序列号 —— 只要能读到就说明 I2C 通信正常、芯片焊接良好
    //             这是产线测试的典型做法：快速验证"通不通"而不是"准不准"
    if (scd94_get_serial(&scd04_dev) == HAL_OK)
    {
        PRINTF("\r\n -----------SCD04 PASS------------------- \r\n");
        return 1;
    }
    PRINTF("\r\n -----------SCD04 FAIL------------------- \r\n");
    return 0;
}

void SCD04_cmd_register(void)
{
    const cmd_item_t scd04cmd = {
        .command = "8",
        .help = " 二氧化碳测试 ",
        .func = &scd04_test,
        .success = 0,
        .fail = 0,
    };
    console_cmd_register((cmd_item_t *)&scd04cmd);
}

void SCD04_INIT(void)
{
    // 【注意】这个"初始化"只是填结构体，没有向传感器发任何指令
    //         因为本项目用"单次测量模式"，不需要预先启动周期测量
    SCD04_init_dev_struct(&scd04_dev, &hi2c2, "SCD04", true);
}
```

**函数8：一次完整的采集（阻塞 5 秒！）**

```c
// 单次采集 == 采集 + 读数据
int scd04_get_Data(void)
{
    // ---- 第1步：发单次测量命令 ----
    if (scd04_send_command(&scd04_dev, SCD4x_COMMAND_MEASURE_SINGLE_SHOT, 2))
    {
        printf("\r\nHAL_ERROR SCD4x_COMMAND_MEASURE_SINGLE_SHOT \r\n");
        return HAL_ERROR;
    }

    HAL_Delay(5000);   // 【关键】必须等 5000ms！
                       // 数据手册规定 measure_single_shot 执行时间 5000ms
                       // 原因：光声法测量需要多次采样求平均，物理上就是慢
                       // 这 5 秒里 CPU 完全阻塞（只有中断能跑）

    // ---- 第2步：发读数据命令 ----
    if (scd04_send_command(&scd04_dev, SCD4x_COMMAND_READ_MEASUREMENT, 2))
    {
        printf("\r\nHAL_ERROR SCD4x_COMMAND_READ_MEASUREMENT \r\n");
        return HAL_ERROR;
    }

    HAL_Delay(1);    // read_measurement 命令自身执行时间 1ms

    // ---- 第3步：读回 9 字节 ----
    if (sensor_recv(&scd04_dev, 9) != HAL_OK)
    {
        printf("\r\n Recv  data error.\r\n");
        return HAL_ERROR;
    }
    // ============ 9 字节数据格式 ============
    //  buffer[0][1] = CO₂ 高低字节    buffer[2] = CRC
    //  buffer[3][4] = 温度 原始值      buffer[5] = CRC
    //  buffer[6][7] = 湿度 原始值      buffer[8] = CRC

    // ---- 第4步：数据转换 ----
    data_co2 = (uint16_t)scd04_dev.buffer[0] << 8 | (uint16_t)scd04_dev.buffer[1];
    // ============ CO₂ 转换公式 ============
    // CO₂[ppm] = 原始 16 位值，直接就是 ppm，不需要任何换算！
    // 例：buffer[0]=0x01, buffer[1]=0xF4 → 0x01F4 = 500 → 500 ppm
    //
    // 【本项目未使用，但补充完整】温湿度的转换公式（数据手册）：
    //   raw_T = buffer[3]<<8 | buffer[4];
    //   温度[°C] = -45 + 175 × raw_T / 65535
    //   （量程 -45°C ~ +130°C 线性映射到 0~65535）
    //
    //   raw_RH = buffer[6]<<8 | buffer[7];
    //   湿度[%RH] = 100 × raw_RH / 65535
    //   （量程 0~100%RH 线性映射到 0~65535）

    memset(scd04_dev.buffer, 0, 9);   // 清缓冲
    return 1;
}
```

**函数9/10：拆分成"采集"和"读数"两步（非阻塞优化）**

```c
// 采集
int scd04_collect(void)
{
    // 只发命令，不等待 —— 让 CPU 立刻返回去干别的
    if (scd04_send_command(&scd04_dev, SCD4x_COMMAND_MEASURE_SINGLE_SHOT, 2))
    {
        printf("\r\nHAL_ERROR SCD4x_COMMAND_MEASURE_SINGLE_SHOT \r\n");
        return HAL_ERROR;
    }
    // 【隐患】成功路径没有 return 语句！返回值不确定。
    //         好在 main.c 里调用时忽略了返回值，不影响运行
}

// 读数据
int scd04_read_data(void)
{
    // 前提：距离上次 scd04_collect() 至少过了 5 秒
    //       在 main.c 里由 SCD04_TIMER_PERIOD = 5（5秒）保证
    if (scd04_send_command(&scd04_dev, SCD4x_COMMAND_READ_MEASUREMENT, 2))
    {
        printf("\r\nHAL_ERROR SCD4x_COMMAND_READ_MEASUREMENT \r\n");
        return HAL_ERROR;
    }

    HAL_Delay(1);
    if (sensor_recv(&scd04_dev, 9) != HAL_OK)
    {
        printf("\r\n Recv  data error.\r\n");
        return HAL_ERROR;
    }

    data_co2 = (uint16_t)scd04_dev.buffer[0] << 8 | (uint16_t)scd04_dev.buffer[1];

    memset(scd04_dev.buffer, 0, 9);
    return 1;
}
```

> **设计精髓**：`scd04_get_Data()` 阻塞 5 秒，只在开机初始化时用一次；主循环里改用 `scd04_collect()` + 等 5 秒(靠定时器计数) + `scd04_read_data()` 的**两阶段状态机**，这样 5 秒的等待期间 CPU 可以正常响应命令、翻页、采其他传感器。这是本项目最巧妙的设计之一，详见 main.c 的 `co2_status` 状态机。

---

## 3.7 【传感器层·单总线】`BSP/Src/DHT11.c` —— 温湿度传感器

### 3.7.1 器件原理与时序（最难的部分）

**DHT11** 使用**单总线（1-Wire）协议**，一根线既发又收，靠**高电平持续时间的长短**来区分 0 和 1。

**完整通信时序：**

```
① 主机起始信号
   主机拉低总线 ≥18ms  ──┐            ┌── 主机拉高 20~40μs
                        └────────────┘
② DHT11 响应
   DHT11 拉低 80μs   ──┐            ┌── DHT11 拉高 80μs
                      └────────────┘
③ 传输 40 位数据（5 字节），每一位的格式：
   数据位"0"： 低电平 50μs + 高电平 26~28μs
   数据位"1"： 低电平 50μs + 高电平 70μs
                        ↑
              【关键】区分 0 和 1 就看高电平多长！
              判定方法：低电平结束后，延时约 40~50μs 再采样：
                  还是高 → 说明高电平 >50μs → 是 "1"
                  已变低 → 说明高电平 <50μs → 是 "0"

④ 结束：DHT11 拉低 50μs 后释放总线
```

**40 位数据格式（5 字节）：**

| 字节 | 含义 |
|---|---|
| `arr[0]` | 湿度整数部分（%RH） |
| `arr[1]` | 湿度小数部分（DHT11 固定为 0） |
| `arr[2]` | 温度整数部分（°C） |
| `arr[3]` | 温度小数部分（bit7=1 表示负温度） |
| `arr[4]` | 校验和 = (arr[0]+arr[1]+arr[2]+arr[3]) & 0xFF |

### 3.7.2 完整源码逐行注释

```c
#include "main.h"
#include "stdint.h"
#include "console.h"

#define READ_CMD "-r"                    // 遗留宏
#define DHT11_GPIO_DATA GPIO_PIN_10      // DHT11 数据线：PD10
#define DHT11_GPIO_BASE GPIOD

// 宏：读取 DHT11 数据线的电平
#define DHT11_GET_DATA HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA)

extern TIM_HandleTypeDef htim1;   // TIM1 用作微秒延时基准（1MHz）

float tem, hum;                   // 【全局变量】温度(°C)、湿度(%RH)
                                  // main.c 的 lcd_set_page0() 读它们显示

#define Delay_ms(x) HAL_Delay(x)  // 毫秒延时用 HAL 的 SysTick
```

**函数1：微秒级延时（本文件的技术核心）**

```c
static void Delay_us(uint16_t us)
{ // 微秒延时
    uint16_t differ = 0xffff - us - 5;
    // ============ 原理讲解 ============
    // TIM1 配置：PSC=63 → 计数频率 1MHz → 每计数 1 次 = 1μs
    //            ARR=65535 → 计满 65536 次溢出
    //
    // 思路：不从 0 数到 us，而是从 (65535 - us - 5) 数到 65530
    //       这样只需要判断"计数值是否 < 65530"，避免了溢出判断的复杂度
    //
    // 减 5 的作用：补偿函数调用、寄存器写入等固有开销（约 5μs）
    //             这是经验值，用示波器实测调出来的
    //
    // 例：us = 50
    //     differ = 65535 - 50 - 5 = 65480
    //     从 65480 数到 65530，共 50 个计数 = 50μs

    __HAL_TIM_SET_COUNTER(&htim1, differ);   // 设定TIM1计数器起始值
                                             // 宏展开：htim1.Instance->CNT = differ
                                             // 直接写 TIM1_CNT 寄存器

    HAL_TIM_Base_Start(&htim1);              // 启动定时器（置位 TIM1_CR1 的 CEN 位）

    while (differ < 0xffff - 5)              // 循环条件：differ < 65530
    {                                        // 判断
        differ = __HAL_TIM_GET_COUNTER(&htim1);  // 查询计数器的计数值
                                                 // 宏展开：differ = htim1.Instance->CNT
                                                 // 计数器每 1μs 加 1，
                                                 // 数到 65530 时循环退出
    }
    HAL_TIM_Base_Stop(&htim1);               // 停止定时器（清 CEN 位）
                                             // 停掉是为了不影响下次使用
}
// 【重要局限】uint16_t 参数最大 65535，即最长延时 65.535ms
//             但代码里有 Delay_us(20000) = 20ms，在范围内，OK
```

**函数2：设置数据线电平**

```c
static void DHT11_BitValue(GPIO_PinState action)
{
    HAL_GPIO_WritePin(DHT11_GPIO_BASE, DHT11_GPIO_DATA, action);
    // action 取 GPIO_PIN_SET(高) 或 GPIO_PIN_RESET(低)
    // 底层：写 GPIOD->BSRR 寄存器（置位/复位寄存器，原子操作，无需读改写）
}
```

**函数3/4：动态切换 GPIO 输入/输出模式（单总线的关键）**

```c
static void DHT11_Init_Out(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();      // 使能 GPIOD 时钟（重复使能无害）
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
                                                 // 底层：GPIOD->MODER[10] = 01(输出)
                                                 //       GPIOD->OTYPER[10] = 0(推挽)
    GPIO_InitStruct.Pin = DHT11_GPIO_DATA;
    GPIO_InitStruct.Pull = GPIO_NOPULL;          // 不用内部上下拉（外部有 4.7k 上拉电阻）
    HAL_GPIO_Init(DHT11_GPIO_BASE, &GPIO_InitStruct);
}

static void DHT11_Init_In(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;      // 浮空输入
                                                 // 底层：GPIOD->MODER[10] = 00(输入)
    GPIO_InitStruct.Pin = DHT11_GPIO_DATA;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;  // 高速（影响输出压摆率，输入模式其实无关）
    HAL_GPIO_Init(DHT11_GPIO_BASE, &GPIO_InitStruct);
    // 【注意】这里没设 Pull，GPIO_InitStruct={0} 使 Pull=GPIO_NOPULL
    //         依赖外部 4.7kΩ 上拉电阻把总线拉高，这是单总线的标准接法
}
// 【为什么要动态切换？】单总线只有一根线，主机发起始信号时必须是输出模式，
//                       DHT11 回数据时主机必须是输入模式，否则会总线冲突（打架）
```

**函数5：读取 1 个字节（8 位）**

```c
static uint8_t DHT11_ReadByte(void)
{
    uint8_t data = 0x00; // 给数据付一个初始值
    uint8_t i = 0;
    for (i = 0; i < 8; i++) // 循环8次，接受一个字节
    {
        // ---- 步骤1：等待本位起始的 50μs 低电平结束 ----
        while (HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA) == GPIO_PIN_RESET)
            ; // 等待低电平结束，进入下一位
              // 【风险】这是死等，如果传感器没插好会永久卡死在这里！
              //         健壮的实现应该加超时计数器

        // ---- 步骤2：延时 50μs 后采样 ----
        Delay_us(50);
        // 【核心判据】
        //   如果是"0"：高电平只有 26~28μs，等 50μs 后早已变回低电平
        //   如果是"1"：高电平有 70μs，等 50μs 后仍然是高电平

        if (HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA) == GPIO_PIN_SET) // 此时检测电平高低，若为高电平
        {
            data |= (0x80 >> i); // 将该位置一
            // 【位操作解析】DHT11 先发高位（MSB first）
            //   i=0 → 0x80>>0 = 0x80 = 1000_0000 → 置 bit7
            //   i=1 → 0x80>>1 = 0x40 = 0100_0000 → 置 bit6
            //   ...
            //   i=7 → 0x80>>7 = 0x01 = 0000_0001 → 置 bit0
            //   用 |= 只置位不影响其他位

            while (HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA) == GPIO_PIN_SET)
                ; // 等待高电平结束，进入下一位
                  // "1"的高电平还剩 70-50=20μs，等它结束
        }
        // 如果是"0"，此刻已经在低电平了，直接进入下一轮 for 循环的"等低电平结束"
    }
    return data; // 返回一个字节的数据
}
```

**函数6：起始信号时序**

```c
static void DHT11_Start(void)
{
    DHT11_Init_Out();               // 主机输出模式
    DHT11_BitValue(GPIO_PIN_RESET); // 主机拉低总线
    Delay_us(20000);                // 延时20ms
                                    // 【为什么是 20ms？】数据手册要求主机至少拉低 18ms，
                                    //   目的是让 DHT11 从低功耗休眠中被唤醒。取 20ms 留余量
    DHT11_BitValue(GPIO_PIN_SET);   // 主机再次拉高总线
    Delay_us(60);                   // 延时20us
                                    // 注释写 20us 但代码是 60us —— 手册要求 20~40μs，
                                    // 60μs 略超但 DHT11 容忍度较大，实测能工作
    DHT11_Init_In();                // 开始时序结束，总线交于DHT11控制
                                    // 切成输入后，外部上拉电阻维持高电平，
                                    // DHT11 检测到主机释放，开始回应
}
```

**函数7：完整读取 + 数据转换（重点）**

```c
void DHT11_Data(float *temp, float *hum)
{
    int8_t i = 0;
    int8_t arr[5];        // 存 5 个字节的原始数据
                          // 【注意】用了 int8_t（有符号），范围 -128~127
                          //   湿度值 20~90 没问题，温度 0~50 没问题
                          //   但如果传感器返回 >127 的值会变成负数 —— 是个隐患
    uint8_t retry = 0;

    DHT11_Start(); // 通信开始时序

    // ---- 等待 DHT11 的 80μs 应答低电平 ----
    while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_RESET && retry < 10000)
    {   //  ① 等 DHT11 拉低（应答低电平）
        retry++;   // 【有超时保护】最多循环 10000 次，防止传感器不在时死锁
    }
    // 【逻辑分析】这个循环的条件是"当前是低电平就继续等"，
    //   所以它实际是在"等低电平结束"（等 80μs 应答低脉冲过去）
    //   注意这里用的是 main.h 里的 DHT11_GPIO_Port/DHT11_Pin 宏
    //   而不是本文件定义的 DHT11_GPIO_BASE/DHT11_GPIO_DATA
    //   —— 说明 CubeMX 里也给 PD10 起了 DHT11 的标签，两套宏指向同一引脚

    // ---- 等待 80μs 应答高电平结束 ----
    while (DHT11_GET_DATA == GPIO_PIN_SET)
        ; // 高电平等待跳变低电平
          // 【风险】这个循环没有超时保护，是死循环隐患

    // ---- 连续读 5 个字节 ----
    for (i = 0; i < 5; i++)
    {
        arr[i] = DHT11_ReadByte();
    }

    Delay_us(50);
    DHT11_Init_Out();                                // 总线交给主机
    DHT11_BitValue(GPIO_PIN_SET);                    // 拉高总线进入空闲
                                                     // 单总线空闲状态必须是高电平

    // ============ 数据转换与校验 ============
    if (arr[0] + arr[1] + arr[2] + arr[3] == arr[4]) // 校验位检测
    {   // 【校验和原理】前 4 字节之和的低 8 位应等于第 5 字节
        //   这是最简单的加和校验（Checksum），能检出大部分单比特错误
        *temp = arr[2];    // 温度整数部分
        *hum = arr[0];     // 湿度整数部分
    }
    // 【设计缺陷】校验失败时不返回错误，下面的代码照样执行 —— 会用到错误数据

    // ---- 温度转换（含负温度处理）----
    if (arr[3] & 0x80)     // 检查温度小数字节的最高位 bit7
    {                      // bit7 = 1 表示负温度（DHT11 的扩展协议，DHT22 更常见）
        *temp = (-1) * ((arr[3] | 0x7F) / 10 + arr[2]);
        // 【公式解析】
        //   本意是：去掉符号位，取小数部分，加上整数部分，再取负
        //   正确写法应该是 (arr[3] & 0x7F) / 10.0 + arr[2]，然后取负
        //   但这里写的是 (arr[3] | 0x7F) —— 用的是"或"！
        //   arr[3] | 0x7F 会把低 7 位全置 1，结果恒为 0xFF 或 0x7F
        //   这是一个明显的笔误（| 和 & 打错）
        //   不过 DHT11 实际不支持负温度（量程 0~50°C），所以这条分支永远不会执行
    }
    else
    {
        *temp = (arr[3] / 10 + arr[2]);
        // 【公式解析】温度 = 整数部分 + 小数部分/10
        //   arr[2] 是整数部分（如 25）
        //   arr[3] 是小数部分（DHT11 恒为 0，DHT22 才有值）
        //   【注意】arr[3]/10 是 int8_t 的整数除法！
        //     如果 arr[3]=5，5/10 = 0（截断），小数部分丢失
        //     正确应写 arr[3] / 10.0f 做浮点除法
        //   对 DHT11 而言 arr[3] 恒为 0，所以温度就是整数值，不受影响
    }

    *hum = arr[0] + arr[1] / 10;
    // 【公式解析】湿度 = 整数部分 + 小数部分/10
    //   同样存在整数除法截断问题，但 DHT11 的 arr[1] 恒为 0，不影响
    //   例：arr[0]=55, arr[1]=0 → hum = 55.0 %RH
}
```

**函数8/9：控制台命令**

```c
static int DHT11_get(int argc, char **argv)
{
    tem = 0, hum = 0;      // 先清零，用于判断是否成功
    PRINTF("\r\n---------温湿度传感器测试--------------- \r\n");

    __disable_irq();       // 【关键】关闭所有中断！
                           // 【为什么？】DHT11 时序是微秒级的，一次中断（比如串口收到
                           //   一个字节需要几微秒到几十微秒）就可能让 Delay_us(50) 
                           //   的采样点错过，读到错误数据。
                           //   关中断保证时序不被打断 —— 这是单总线驱动的标准做法。
    DHT11_Data(&tem, &hum);
    __enable_irq();        // 立刻恢复中断

    Delay_ms(2000);        // 延时 2 秒
                           // 【为什么？】DHT11 采样周期不能小于 1 秒（数据手册要求 ≥1s，
                           //   建议 2s），否则传感器内部还没完成新的转换，
                           //   会返回上次的旧数据甚至通信失败

    PRINTF("\r\n* TEM: %.1f HUM: %.1f \r\n", tem, hum);
    if (tem)               // 温度非 0 就认为成功
    {                      // 【局限】如果环境真的是 0°C，会误判为失败
        PRINTF("\r\n -----------DHT11 PASS------------------- \r\n");
        return 1;
    }
    else
    {
        PRINTF("\r\n -----------DHT11 FAIL------------------- \r\n");
    }
    return 0;
}

void dht11_cmd_register(void)
{
    const cmd_item_t cmd = {
        .command = "4",
        .help = " 温湿度传感器 ",
        .func = &DHT11_get,
        .success = 0,
        .fail = 0,
    };
    console_cmd_register((cmd_item_t *)&cmd);
}
```

---

## 3.8 【模块层·串口】`BSP/Src/bmp280.c` —— 大气压模块

### 文件定位

**注意**：虽然叫 bmp280（这是个 I2C/SPI 芯片名），但**本项目的 BMP280 是一个带 MCU 的智能模块，通过 USART10 输出 ASCII 文本**，而不是直接接 I2C。所以这个文件本质是一个**串口文本协议解析器**。

模块输出格式大概是：
```
Pressure : 101325 Pa Altitude : 12.34 m
   ↑  ↑   ↑    ↑
 词0 词1 词2  ...  词5
```

### 完整源码逐行注释

```c
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bmp280.h"

#include "main.h"
#include "console.h"

static uint8_t gn_buffer[BMP_BUFFER_LEN];  // 【已完成帧】的备份缓冲，供测试命令打印
uint8_t bmp280_buffer[BMP_BUFFER_LEN];     // 【正在接收】的缓冲，中断里逐字节填充
uint8_t bmp280_count;                      // 计数位：当前已收到的字节数（也是写入下标）
uint8_t bmp280_temp;                       // 缓存字符：HAL_UART_Receive_IT 的单字节落点
bool bmp280_frame_ready = false;           // 帧就绪标志：是否已检测到帧头，正在接收帧体
static bool receive_success = false;       // 是否成功收到过数据（用于 PASS/FAIL 判定）

extern UART_HandleTypeDef huart10;         // BMP280 挂在 USART10

int pressure_Data = -1;                    // 【全局】气压值，单位 Pa
double height_Data = -1;                   // 【全局】海拔高度，单位 m
#define BMP280_HEAD "Pressure"             // 帧头字符串（定义了但代码里用的是首字符 'P' 判断）
```

**函数1：从文本中提取数字（strtok 分词）**

```c
static void get_pressure_number(char *input, int *pressure_Data, double *height_Data)
{
    char *token;
    int num = 0;         // 词序号计数器

    // 第一次调用strtok，以空格为分隔符
    token = strtok(input, " ");
    // 【strtok 工作原理】
    //   第一次调用传入字符串首地址，它会找到第一个分隔符，把它改成 '\0'，
    //   返回第一个词的起始地址。同时用一个内部 static 指针记住位置。
    //   后续调用传 NULL，它从记住的位置继续找下一个词。
    //   【副作用】strtok 会修改原字符串！所以这里传的是拷贝出来的 buffer

    while (token != NULL)     // NULL 表示没有更多词了
    {
        // 如果是数字则转换并终止循环
        if (num == 2)         // 第 3 个词（下标从0开始）是气压数值
        {
            *pressure_Data = atoi(token);   // ASCII to Integer，字符串转整数
                                            // "101325" → 101325
        }

        if (num == 5)         // 第 6 个词是海拔数值
        {
            *height_Data = atof(token);     // ASCII to Float，字符串转浮点
                                            // "12.34" → 12.34
        }

        num++;
        token = strtok(NULL, " ");   // 取下一个词
    }
    // 【硬编码风险】这里靠"第几个词"来定位数据，
    //   完全依赖模块输出格式固定不变。如果模块固件升级改了输出格式，解析就全错了。
    //   更健壮的做法是查找 "Pressure" 关键字后面的第一个数字。
}
```

**函数2：处理接收缓冲（中断里被调用）**

```c
void bmp280_deal_buffer(void)
{
    /* 获取第一个子字符串 */
    char buffer[128] = {0};                          // 栈上临时缓冲

    memset(gn_buffer, 0, BMP_BUFFER_LEN);            // 清空备份区
    memcpy(gn_buffer, bmp280_buffer, bmp280_count);  // 备份原始帧（供 bmp280_test 打印）
    memcpy(buffer, bmp280_buffer, bmp280_count);     // 再拷一份给 strtok 用
                                                     // 【为什么拷两份？】因为 strtok 会
                                                     //   破坏字符串（插入 '\0'），
                                                     //   拷一份专门给它糟蹋，
                                                     //   gn_buffer 保持完整供打印

    memset(bmp280_buffer, 0, bmp280_count);          // 清空接收缓冲，准备下一帧

    get_pressure_number(buffer, &pressure_Data, &height_Data);   // 解析出数值

    bmp280_count = 0;             // 计数归零
    bmp280_temp = 0;              // 单字节缓存清零
    bmp280_frame_ready = false;   // 重置帧标志，等待下一个帧头
}
// 【重要】这个函数在【中断上下文】中被调用（见 main.c 的 HAL_UART_RxCpltCallback）
//   它做了 memset/memcpy/strtok/atoi 等相对耗时的操作，
//   在中断里做这些事不是最佳实践（可能导致其他中断响应延迟），
//   但对本项目的低速场景是可以接受的。
```

**函数3/4：测试命令与注册**

```c
static int bmp280_test()
{
    PRINTF("\r\n---------大气压传感器测试--------------- \r\n");

    printf("\r\n* %s\r\n", gn_buffer);   // 打印最近收到的完整原始帧

    if (receive_success)     // 【BUG】receive_success 从未被置为 true！
    {                        //   初值 false，bmp280_deal_buffer 里也没设置它
                             //   所以这里永远走 FAIL 分支
                             //   （对比 csb.c 里就有 receive_success = true）
        PRINTF("\r\n -----------BMP280 PASS------------------- \r\n");
        return 1;
    }
    PRINTF("\r\n -----------BMP280 FAIL------------------- \r\n");
    return 0;
}

void bmp280_cmd_register()
{
    const cmd_item_t cmd = {
        .command = "2",
        .help = "大气压传感器",
        .func = &bmp280_test,
        .success = 0,
        .fail = 0,
    };
    console_cmd_register((cmd_item_t *)&cmd);
}
```

### 中断侧的帧同步逻辑（在 main.c 中）

```c
if (huart == &huart10)
{
    if (bmp280_temp == 0x50)          // 0x50 = 'P'，"Pressure" 的首字母
    {
        bmp280_frame_ready = true;    // 检测到帧头，开始接收
    }

    if (bmp280_frame_ready)
    {
        bmp280_buffer[bmp280_count++] = bmp280_temp;   // 存入缓冲
        if (bmp280_temp == 0x0D)      // 0x0D = '\r' 回车，帧尾
        {
            bmp280_deal_buffer();     // 一帧收完，解析
        }
    }

    HAL_UART_Receive_IT(&huart10, &bmp280_temp, 1);   // 【必须】重新启动下一次接收
}
```

> **中断接收模式的核心套路**：`HAL_UART_Receive_IT(..., 1)` 是"一次性"的，收到 1 字节后中断标志会被关闭。所以**每次回调的最后必须再调用一次**，形成"自我续期"的链条，否则只能收到 1 个字节就再也收不到了。

---

## 3.9 【模块层·串口】`BSP/Src/csb.c` —— 超声波测距模块

### 器件原理

超声波模块（波特率 9600），主动周期输出 **4 字节二进制帧**：

| 字节 | 含义 |
|---|---|
| `[0]` | 帧头 `0xFF` |
| `[1]` | 距离高 8 位 |
| `[2]` | 距离低 8 位 |
| `[3]` | 校验和 = (帧头 + 高字节 + 低字节) & 0xFF |

距离单位为 **mm**。

### 完整源码逐行注释

```c
#include "csb.h"

#include "main.h"
#include "console.h"

uint8_t gn_buffer[CSB_BUFFER_LEN];   // 【注意】这里没有 static！是全局符号
                                     // 而 gnss.c 和 bmp280.c 里也定义了 static gn_buffer
                                     // 幸好那两个是 static（文件作用域），
                                     // 否则会产生重复定义的链接错误
uint8_t csb_buffer[CSB_BUFFER_LEN];  // 接收缓冲
uint8_t csb_count;                   // 计数位
uint8_t csb_temp;                    // 缓存字符（中断落点）
bool csb_frame_ready = false;        // 帧就绪标志
static bool receive_success = false; // 是否成功收到数据
extern bool fs_status;               // 【引用 main.c 的全局变量】
                                     // fs_status = "蜂鸣器/全亮"状态（KEY3 控制）
                                     // 用来做互斥：手动模式下超声波不抢灯

void csb_deal_buffer()
{
    memset(gn_buffer, 0, CSB_BUFFER_LEN);          // 清备份区
    memcpy(gn_buffer, csb_buffer, csb_count);      // 备份这一帧

    memset(csb_buffer, 0, csb_count);              // 清接收缓冲
    csb_count = 0;
    csb_temp = 0;

    csb_frame_ready = false;      // 重置帧标志
    receive_success = true;       // 【标记收到过数据】← bmp280.c 缺的就是这句

    // ============ 距离解析 ============
    int distance = (int)gn_buffer[1] << 8 | (int)gn_buffer[2];
    // gn_buffer[0] = 0xFF 帧头
    // gn_buffer[1] = 距离高字节
    // gn_buffer[2] = 距离低字节
    // 大端拼接：高字节左移 8 位 或上 低字节
    // 例：[1]=0x01, [2]=0x2C → 0x012C = 300 → 300mm = 30cm
    // 【注意】没有验证 gn_buffer[3] 的校验和，缺少数据完整性检查

    // ============ 自动控制逻辑：距离 < 30cm 就亮全部灯 ============
    if (!fs_status)     // 只有在"非手动模式"下才自动控制
    {                   // 如果用户按了 KEY3 强制全亮，这里就不干预（避免抢控制权）
        if (distance < 300)     // 距离小于 300mm = 30cm
        {
            HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_SET);    // 绿灯亮
            HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_SET);  // 黄灯亮
            HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_SET);        // 红灯亮
        }
        else
        {
            HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_RESET);  // 全灭
            HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_RESET);
        }
    }
}

static int csb_test(int argc, char **argv)
{
    PRINTF("\r\n---------超声波传感器测试--------------- \r\n");

    int distance = gn_buffer[1] << 8 | gn_buffer[0];
    // 【BUG】这里的字节顺序和 csb_deal_buffer 里不一致！
    //   deal_buffer 用的是 [1]<<8 | [2]   （正确）
    //   这里用的是   [1]<<8 | [0]   （把帧头 0xFF 当低字节了）
    //   所以打印出来的距离值是错的（会多出 0xFF = 255）
    //   例：正确 300mm，这里会算成 0x01FF = 511

    printf("\r\n*超声波测试距离：%dmm\r\n", distance);

    if (receive_success)      // 只要收到过帧就算 PASS
    {
        PRINTF("\r\n -----------超声波 PASS------------------- \r\n");
        return 1;
        // 【逻辑瑕疵】这里 return 了，下面的 receive_success = false 永远执行不到
        //   所以一旦 PASS 过一次，后续即使传感器拔掉也永远 PASS
    }
    PRINTF("\r\n -----------超声波 FAIL------------------- \r\n");

    receive_success = false;  // 死代码（unreachable after return above in PASS path）
    return 0;
}

void csb_cmd_register()
{
    const cmd_item_t cmd = {
        .command = "3",
        .help = " 超声波传感器 ",
        .func = &csb_test,
        .success = 0,
        .fail = 0,
    };
    console_cmd_register((cmd_item_t *)&cmd);
}
```

**`csb.h`：**
```c
#ifndef CSB_H
#define CSB_H

#include <stdbool.h>
#include "main.h"

#define CSB_BUFFER_LEN 128     // 缓冲区大小
#define CSB_FRAME_LEN 4        // 【关键】一帧固定 4 字节

extern uint8_t csb_buffer[CSB_BUFFER_LEN];
extern uint8_t csb_count;
extern uint8_t csb_temp;
extern bool csb_frame_ready;

void csb_cmd_register(void);
void csb_deal_buffer(void);

#endif
```

### 中断侧帧同步（main.c）

```c
if (huart == &huart5)
{
    if (csb_temp == 0xFF)         // 检测到帧头 0xFF
    {
        csb_frame_ready = true;
    }

    if (csb_frame_ready)
    {
        csb_buffer[csb_count++] = csb_temp;
        if (csb_count == CSB_FRAME_LEN)   // 【定长帧】收满 4 字节就处理
        {
            csb_deal_buffer();
        }
    }

    HAL_UART_Receive_IT(&huart5, &csb_temp, 1);
}
```

> **定长帧 vs 变长帧**：超声波用"帧头 + 固定长度"（收满 4 字节即完成），BMP280/GNSS 用"帧头 + 帧尾"（遇到 `\r` 才完成）。这是串口协议解析的两大基本套路。

---

## 3.10 【模块层·串口】`BSP/Src/gnss.c` —— GNSS 卫星定位

### 器件原理

GNSS 模块输出标准 **NMEA-0183** 协议语句，每条以 `$` 开头，`\r\n` 结尾：

```
$GNRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A,V*57
 ↑ 帧头  时间   状态  纬度    N/S  经度    E/W  速度 航向  日期      校验
```

本项目只关心 `$GNRMC`（推荐最小定位信息），且**只做"能收到就算通过"的连通性测试**，不解析经纬度。

### 完整源码逐行注释

```c
#include "gnss.h"

#include "main.h"
#include "console.h"

static uint8_t gn_buffer[GNSS_BUFFER_LEN];  // 备份区（static 文件私有）
uint8_t gnss_buffer[GNSS_BUFFER_LEN];       // 接收缓冲，1024 字节
uint8_t gnss_count;                         // 计数位
                                            // 【隐患】uint8_t 最大 255，
                                            //   而缓冲区是 1024 字节，
                                            //   如果一帧超过 255 字节会溢出回绕
                                            //   NMEA 单句一般 <82 字节，勉强安全
uint8_t gnss_temp;                          // 缓存字符
bool gnss_frame_ready = false;              // 帧就绪标志
static bool gnss_success = false;           // 是否成功收到过完整帧

extern UART_HandleTypeDef huart7;           // 【注意】这里 extern 的是 huart7，
                                            //   但实际 GNSS 接的是 huart4（见 main.c）
                                            //   这个 extern 声明在本文件里没被用到，属于无用代码

#define GNSS_DATA_HEAD "$GNRMC,"            // 目标帧头，7 个字符

// 检查报文开头
uint8_t gnss_check_frame()
{
    if (memcmp(gnss_buffer, GNSS_DATA_HEAD, GNSS_DATA_HEAD_LEN))
    {   // memcmp 返回 0 表示相同，非 0 表示不同
        // 这里 if 条件为"非0"即"不匹配"
        return 1;      // 返回 1 = 不是我要的帧
    }
    return 0;          // 返回 0 = 匹配成功
}
// 【返回值语义反直觉】check 函数一般"成功返回1"，
//   这里是"成功返回0"（沿用了 memcmp 的语义），
//   所以调用方用的是 if(!gnss_check_frame()) 双重否定

// 处理gnss_buffer缓冲区
void gnss_deal_buffer()
{
    memset(gn_buffer, 0, GNSS_BUFFER_LEN);        // 清空备份区
    memcpy(gn_buffer, gnss_buffer, gnss_count);   // 备份完整帧

    memset(gnss_buffer, 0, gnss_count);           // 清空接收缓冲
    gnss_count = 0;
    gnss_temp = 0;
    gnss_frame_ready = false;                     // 重置帧标志
    gnss_success = true;                          // 标记成功收到过数据
}

int gnss_test()
{
    PRINTF("-------gnss卫星测试---------");

    printf("\r\n* %s\r\n", gn_buffer);    // 打印最近收到的 NMEA 语句原文

    if (gnss_success)
    {
        PRINTF("\r\n -----------GNSS PASS------------------- \r\n");
        return 1;
    }
    PRINTF("\r\n -----------GNSS FAIL------------------- \r\n");
    return 0;
}

void gnss_cmd_register()
{
    const cmd_item_t cmd = {
        .command = "6",
        .help = " GNSS卫星",
        .func = &gnss_test,
        .success = 0,
        .fail = 0,
    };
    console_cmd_register((cmd_item_t *)&cmd);
}
```

**`gnss.h`：**
```c
#ifndef GNSS_H
#define GNSS_H

#include <stdbool.h>
#include "main.h"

#define GNSS_BUFFER_LEN 1024      // 缓冲区 1KB
#define GNSS_FRAME_LEN 20         // 定义了但未使用
#define GNSS_DATA_HEAD_LEN 7      // "$GNRMC," 的长度

extern uint8_t gnss_buffer[GNSS_BUFFER_LEN];
extern uint8_t gnss_count;
extern uint8_t gnss_temp;
extern bool gnss_frame_ready;

uint8_t gnss_check_frame(void);
void gnss_deal_buffer(void);
void gnss_cmd_register(void);

#endif
```

### 中断侧帧同步（main.c）—— 三级过滤，最精巧的一个

```c
if (huart == &huart4)
{
    // ---- 第1级：检测 '$' 帧起始符 ----
    if (gnss_temp == 0x24)        // 0x24 = '$'，所有 NMEA 语句的起始符
    {
        gnss_frame_ready = true;  // 开始接收
    }

    if (gnss_frame_ready)
    {
        gnss_buffer[gnss_count++] = gnss_temp;

        // ---- 第2级：收满 7 字节时校验是不是 "$GNRMC," ----
        if (gnss_count == GNSS_DATA_HEAD_LEN)   // 刚好收到 7 个字节
        {
            if (!gnss_check_frame())    // 双重否定：check返回0(匹配)→!0=真
            {                           // 【逻辑BUG】这个判断是反的！
                                        //   check 返回 0 表示"匹配成功"，
                                        //   但这里匹配成功却去丢弃帧
                gnss_count = 0;             // 丢弃
                gnss_frame_ready = false;   // 重新等下一个 '$'
            }
            // 正确逻辑应该是：if (gnss_check_frame()) { 丢弃 }
            // 即"不匹配时丢弃"。当前实现是"匹配时丢弃"，
            // 结果反而会接收所有非 $GNRMC 的语句（如 $GNGGA、$GPGSV）
        }
        // ---- 第3级：遇到 '\r' 结束一帧 ----
        else if (gnss_temp == 0x0D)   // 0x0D = '\r' 回车
        {
            gnss_deal_buffer();       // 一帧完成
        }
    }

    HAL_UART_Receive_IT(&huart4, &gnss_temp, 1);   // 续期
}
```

> 因为测试只判断"有没有收到数据"，这个逻辑反转的 BUG 不影响 PASS/FAIL 结果，属于隐藏缺陷。

---

## 3.11 【模块层·串口】`BSP/Src/EB21.c` —— 星闪（NearLink）模块

### 器件原理

**EB21 星闪模块** 是华为主导的 NearLink 短距通信模块，通过 **UART7 + AT 指令** 控制。`AT+MAC?` 用于查询模块的 MAC 地址——能返回就说明模块在线。

### 完整源码逐行注释

```c
#include "EB21.h"

#include "main.h"
#include "console.h"

uint8_t xs_buffer[XS_BUFFER_LEN];   // 接收缓冲（xs = 星闪拼音首字母）
uint16_t xs_count = 0;              // 计数位（这里用的是 uint16_t，比其他模块更安全）
uint8_t xs_temp = 0;                // 缓存字符

#define XS_TEST_CMD "AT+MAC?\r\n"   // AT 指令：查询 MAC 地址
                                    // 【AT 指令规范】必须以 \r\n 结尾，模块才认为指令输入完毕

extern UART_HandleTypeDef huart7;   // 星闪模块挂在 UART7
extern UART_HandleTypeDef huart1;   // 声明了但未使用

static int xs_get_msg(int argc, char **argv)
{
    printf("\r\n-----------------星闪测试-----------\r\n");

    // ---- 步骤1：清空接收缓冲 ----
    memset(xs_buffer, 0, xs_count);   // 只清掉之前收到的字节数
    xs_count = 0;
    xs_temp = 0;

    // ---- 步骤2：发送 AT 指令 ----
    HAL_UART_Transmit(&huart7, XS_TEST_CMD, strlen(XS_TEST_CMD), 1000);
    // 【编译警告】XS_TEST_CMD 是 const char*，
    //   而 HAL_UART_Transmit 第2个参数要求 uint8_t*，
    //   这里没有强制类型转换，会产生 warning（但功能正常）
    // strlen("AT+MAC?\r\n") = 9 字节
    // 超时 1000ms

    // ---- 步骤3：等待模块回应 ----
    HAL_Delay(1000);   // 【简单粗暴的同步方式】固定等 1 秒
                       // 这 1 秒里，UART7 的接收中断会把回应字节
                       // 逐个填进 xs_buffer（见 main.c 的回调）
                       // 更优雅的做法是轮询检查 xs_count 变化 + 超时

    // ---- 步骤4：打印结果 ----
    printf("\r\n* %s\r\n", XS_TEST_CMD);   // 回显发出去的指令
    printf("\r\n* %s\r\n", xs_buffer);     // 打印模块的回应

    // ---- 步骤5：判定 ----
    if (strlen(xs_buffer))    // 只要缓冲区里有内容（长度非0）就算成功
    {                         // 【同样有类型警告】strlen 要 const char*，传的是 uint8_t*
        PRINTF("\r\n -----------EB21 PASS------------------- \r\n");
        return 1;
    }
    else
    {
        PRINTF("\r\n -----------EB21 FAIL------------------- \r\n");
    }

    return 0;
}

void xs_cmd_register(void)
{
    const cmd_item_t cmd = {
        .command = "5",
        .help = " 星闪模块 ",
        .func = &xs_get_msg,
        .success = 0,
        .fail = 0,
    };
    console_cmd_register((cmd_item_t *)&cmd);
}
```

### 中断侧（main.c）—— 最简单的"无脑接收"

```c
if (huart == &huart7)
{
    xs_buffer[xs_count++] = xs_temp;    // 不做任何协议解析，收到啥存啥
                                        // 【溢出风险】没有边界检查！
                                        //   如果模块持续输出超过 XS_BUFFER_LEN 字节，
                                        //   会越界写内存，破坏其他变量
    HAL_UART_Receive_IT(&huart7, &xs_temp, 1);
    return;
}
```

---

## 3.12 【模块层·串口】`BSP/Src/lcd.c` —— 串口组态屏

### 器件原理

这是一款**组态型串口液晶屏**（如大彩、迪文等品牌），通过 USART3 收发 ASCII 指令：

| 指令 | 格式 | 作用 |
|---|---|---|
| 设置文本 | `SET_TXT(控件ID,'内容');\r\n` | 修改指定控件的显示文字 |
| 页面跳转 | `JUMP(页号);\r\n` | 切换到指定画面 |
| 屏回应 | `OK\r\n` | 屏收到并执行成功 |

### 完整源码逐行注释

```c
#include "lcd.h"

#include "main.h"
#include "console.h"

uint8_t lcd_buffer[LCD_BUFFER_LEN];   // 接收缓冲，20 字节
uint8_t lcd_count;                    // 计数位
uint8_t lcd_temp;                     // 缓存字符
bool lcd_ready = true;                // 【流控核心】LCD 是否空闲可接收新指令
                                      // 初值 true = 假定开机时屏是就绪的

extern UART_HandleTypeDef huart3;     // LCD 挂在 USART3

#define LCD_REPLAY "OK\r\n"           // 屏执行成功后的回应，4 个字符

char *strArray[15] = {0}; // 指针数组   ← 定义了但完全没用到（遗留代码）
int gn_count = 1;                     // 同上，未使用

// ============ 函数1：检查屏的回应帧 ============
uint8_t lcd_check_frame(void)
{
    // 字符串比较，看看是不是我要的那一帧
    if (0 == (memcmp(&lcd_buffer, LCD_REPLAY, 4)))
    {   // memcmp 返回 0 = 内容相同 = 收到了 "OK\r\n"
        lcd_ready = true;                  // 【置就绪】屏已处理完，可以发下一条了
        memset(lcd_buffer, 0, lcd_count);  // 清缓冲
        lcd_count = 0;
        lcd_temp = 0;
        return 1;      // 成功
    }
    // 收到的不是 "OK\r\n"（可能是错误信息或噪声）
    memset(lcd_buffer, 0, lcd_count);      // 照样清空，避免脏数据累积
    lcd_count = 0;
    lcd_temp = 0;

    return 0;          // 失败
}
```

**函数2：LCD 测试命令**

```c
void lcd_test()      // 【类型不一致】声明为 void 却有 return 1/0，
                     //   而 cmd_item_t.func 要求是 int(*)(int, char**)
                     //   编译器会警告，但 ARM ABI 下返回值在 R0，实际能工作
{
    char temp[100] = {0};

    // 构造测试用的显示指令：显示固定的 99.9°C / 99.9RH / 999lx
    sprintf(temp, "SET_TXT(4,'99.9\xA1\xE3\x43');SET_TXT(5,'99.9RH');SET_TXT(6,'999lx');\r\n");
    // ============ 转义字符解析 ============
    //   \xA1\xE3 是 GB2312 编码的"°"（度符号）
    //     GB2312 中 0xA1E3 = "°"
    //   \x43 = 'C'
    //   所以 "99.9\xA1\xE3\x43" 在屏上显示为 "99.9°C"
    //   【为什么用 GB2312？】国产串口屏普遍用 GB2312/GBK 字库，不是 UTF-8

    PRINTF("\r\n------------LCD显示屏测试--------------- \r\n");

    memset(lcd_buffer, 0, lcd_count);        // 清缓冲（用 lcd_count 长度）
    lcd_ready = false;                       // 【置忙】准备发指令，等屏回 OK
    memset(lcd_buffer, 0, LCD_BUFFER_LEN);   // 再彻底清一次（用完整长度）
    lcd_count = 0;
    lcd_temp = 0;

    HAL_UART_Transmit(&huart3, (const uint8_t *)temp, strlen(temp), 100);   // 发指令
    HAL_Delay(2000);   // 等 2 秒，给屏充足的响应时间
                       // 期间 USART3 接收中断会收到 "OK\r\n"，
                       // 累计 4 字节后触发 lcd_check_frame() 把 lcd_ready 置 true

    if (lcd_ready)     // 检查是否收到了 OK
    {
        PRINTF("\r\n -----------LCD PASS------------------- \r\n");
        return 1;
    }

    PRINTF("\r\n -----------LCD FAIL------------------- \r\n");

    return 0;
}

void lcd_cmd_register()
{
    const cmd_item_t cmd = {
        .command = "7",
        .help = " lcd卫星",     // 帮助文本笔误，应该是 "lcd显示屏"（复制 gnss 时忘改）
        .func = &lcd_test,
        .success = 0,
        .fail = 0,
    };
    console_cmd_register((cmd_item_t *)&cmd);
}
```

**函数3/4：页面切换与内容填充**

```c
// 翻到哪一页
void lcd_switch(int lcd_page)
{
    char temp[128] = {0};

    /* 等待LCD就绪 */
    if (!lcd_ready)
    {
        // 没准备好，可能丢包了，也可能没到，延时一段时间，直接发就好
        HAL_Delay(500);    // 【简易流控】等 500ms 后无论如何都发
                           // 优点：不会死锁（不像 while(!lcd_ready) 那样可能永久卡住）
                           // 缺点：500ms 是硬编码经验值，如果屏更慢就会丢指令
    }

    sprintf(temp, "JUMP(%d);\r\n", lcd_page);   // 构造跳转指令，如 "JUMP(1);\r\n"
    HAL_UART_Transmit(&huart3, (const uint8_t *)temp, strlen(temp), 100);
    lcd_ready = false;   // 【置忙】发完立刻标记忙，等屏回 OK 才会被中断置回 true
}

// 填什么内容
void lcd_set_data(char *buffer)
{
    if (!lcd_ready) // 屏未就绪则先延时
    {
        HAL_Delay(500);
    }
    HAL_UART_Transmit(&huart3, (const uint8_t *)buffer, strlen(buffer), 100); // 下发文本帧
    lcd_ready = false;   // 标记忙，等屏回 OK
}
```

**`lcd.h`：**
```c
#ifndef LCD_H
#define LCD_H

#include <stdbool.h>
#include "main.h"

#define LCD_BUFFER_LEN 20      // 接收缓冲大小
#define LCD_REPLAY_LEN 4       // "OK\r\n" 的长度，中断里靠它判断收满

extern uint8_t lcd_buffer[LCD_BUFFER_LEN];
extern uint8_t lcd_count;
extern uint8_t lcd_temp;
extern bool lcd_frame_ready;   // 【声明了但 lcd.c 里定义的是 lcd_ready，名字不一致】
                               //   幸好没有任何文件真的去用 lcd_frame_ready，
                               //   否则会链接错误

uint8_t lcd_check_frame(void);
void lcd_cmd_register(void);
void lcd_switch(int lcd_page);
void lcd_set_data(char *buffer);

#endif
```

### 中断侧（main.c）—— 定长回应帧

```c
if (huart == &huart3) // LCD
{
    lcd_buffer[lcd_count++] = lcd_temp;
    if (lcd_count == LCD_REPLAY_LEN)     // 收满 4 字节
    {
        lcd_check_frame(); // 查看LCD是否准备就绪
    }
    HAL_UART_Receive_IT(&huart3, &lcd_temp, 1);
    return;
}
```

---

## 3.13 【应用层】`BSP/Src/console.c` + `console.h` —— 命令行控制台框架

### 文件定位

**整个项目的应用层大脑**。它提供了一套完整的、可扩展的命令行系统：
- 用**链表**管理命令，任何模块都能动态注册自己的命令
- 支持退格键编辑
- 支持 ANSI 转义序列做终端颜色/清屏
- 内置 `help` / `clear` / `show` / `auto` 四个系统命令

### 3.13.1 `console.h` 完整解析

```c
#ifndef __SHELL_H
#define __SHELL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "queue.h"      // 用环形队列取输入
#include "usr_uart.h"   // printf 重定向
#include "list.h"       // 双向链表

// ============ 特殊按键的字节序列 ============
// 终端软件（SecureCRT/Putty/串口助手）按方向键时会发送 3 字节 ANSI 转义序列
#define KEY_UP    "\x1b\x5b\x41"    /* [up] key: ESC [ A */
#define KEY_DOWN  "\x1b\x5b\x42"    /* [down] key: ESC [ B */
#define KEY_RIGHT "\x1b\x5b\x43"    /* [right] key: ESC [ C */
#define KEY_LEFT  "\x1b\x5b\x44"    /* [left] key: ESC [ D */
                                    // 0x1b = ESC, 0x5b = '['
                                    // 【注意】这4个宏定义了但 console_run 里没实现方向键处理
#define KEY_ENTER     '\r'          /* 回车键 = 0x0D */
#define KEY_BACKSPACE '\b'          /* 退格键 = 0x08 */

#define PRINTF(...)  printf(__VA_ARGS__)     // 可变参数宏，__VA_ARGS__ 展开所有参数
#define SPRINTF(...) sprintf(__VA_ARGS__)

// 【错误信息宏】红色显示 + 自动加 [ERR] 前缀
#define ERR(fmt)                      \
    do                                \
    {                                 \
        TERMINAL_FONT_RED();          /* 切红色 */ \
        PRINTF("[ERR]: " fmt "\r\n"); /* 字符串字面量拼接 */ \
        TERMINAL_FONT_WHITE();        /* 切回白色 */ \
    } while (0)
// 【do{...}while(0) 的作用】让宏在语法上等价于单条语句，
//   这样 if(x) ERR("bad"); else ... 才不会报错

#define INFO(fmt)                     \
    do                                \
    {                                 \
        TERMINAL_FONT_CYAN();         \
        PRINTF("[SYS]: " fmt "\r\n"); \
        TERMINAL_FONT_WHITE();        \
    } while (0)

// ============ ANSI 转义序列：终端字体颜色 ============
// 格式：ESC [ 参数 m       ESC = \033 = 0x1B
// 参数：0=正常 1=加粗   30-37=前景色  40-47=背景色
#define TERMINAL_FONT_BLACK()  PRINTF("\033[1;30m")
#define TERMINAL_FONT_L_RED()  PRINTF("\033[0;31m")   /* light red 不加粗的红 */
#define TERMINAL_FONT_RED()    PRINTF("\033[1;31m")   /* red 加粗红 */
#define TERMINAL_FONT_GREEN()  PRINTF("\033[1;32m")
#define TERMINAL_FONT_YELLOW() PRINTF("\033[1;33m")
#define TERMINAL_FONT_BLUE()   PRINTF("\033[1;34m")
#define TERMINAL_FONT_PURPLE() PRINTF("\033[1;35m")
#define TERMINAL_FONT_CYAN()   PRINTF("\033[1;36m")
#define TERMINAL_FONT_WHITE()  PRINTF("\033[1;37m")

/* 清除从光标到行尾的内容 —— 用于实现退格删除 */
#define TERMINAL_CLEAR_END() PRINTF("\033[K")

/* 清除整个屏幕 */
#define TERMINAL_DISPLAY_CLEAR() PRINTF("\033[2J")

/* 光标复位到左上角 (Home) */
#define TERMINAL_RESET_CURSOR() PRINTF("\033[H")

/* 反显（前景背景色互换）—— 用于突出显示欢迎语 */
#define TERMINAL_HIGH_LIGHT()    PRINTF("\033[7m")
#define TERMINAL_UN_HIGH_LIGHT() PRINTF("\033[27m")

#define COMMAND_LEN 24     // 命令名最大长度
#define HELP_LEN 200       // 帮助文本最大长度

// 【命令回调函数指针类型】仿照 C 的 main(argc, argv) 设计
typedef int (*console_cmd_func_t)(int argc, char **argv);
// 返回值约定：1 = 成功/PASS，0 = 失败/FAIL

// ============ 命令项结构体 ============
typedef struct cmd_item_
{
    char command[COMMAND_LEN];   // 命令名，如 "1" "help"
    char help[HELP_LEN];         // 帮助文本
    console_cmd_func_t func;     // 命令处理函数指针
    struct list_head list;       // 【侵入式链表节点】把自己挂到 cmd_list 上
    unsigned char success;       // 成功次数计数器（auto 测试时累加）
    unsigned char fail;          // 失败次数计数器
} cmd_item_t;

void console_init(void);
void console_cmd_register(cmd_item_t *cmd);
void console_run(void);

#endif /*__SHELL_H*/
```

### 3.13.2 `console.c` 完整逐行解析

```c
#include "console.h"
#include "queue.h"

#define HANDLE_LEN 128    // 定义了但未使用
#define MAX_ARGS 6        // 最多解析 6 个参数（含命令名本身）

// 【命令行输入缓冲】
__packed typedef struct
{
    uint8_t buff[BUFFER_SIZE];   // 128 字节，存用户正在输入的命令行
    int len;                     // 当前已输入的字符数
} HANDLE_TYPE_S;

static struct list_head cmd_list;      // 【命令链表头】所有注册的命令都挂在这
static HANDLE_TYPE_S Handle = {0};     // 输入缓冲实例

// 引用 split_argv.c 里的分词函数
extern size_t console_split_argv(char *line, char **argv, size_t argv_size);
```

**函数1：`console_run()` —— 主循环调用的核心（命令行状态机）**

```c
void console_run(void)
{
    char data = 0;
    cmd_item_t *pos, *ppos;   // 链表遍历用的两个游标

    /* 通过死循环一个一个读出所有串口数据 */
    while (queue_out(&data))    // 【生产者-消费者对接点】
    {                           // 从环形队列取字符，队列空时返回 false 退出循环
                                // 每次主循环把队列里积压的字符全部取完

        if (Handle.len > BUFFER_SIZE)
        {
            /* 缓冲区太满，提示对方输入超出限制 */
            break;    // 【隐患】只是 break 不处理，也没真的打印提示，
                      //   而且判断条件应该是 >= BUFFER_SIZE-1 才安全
        }

        switch (data)
        {
        // ---- 情况1：收到退格键 ----
        case KEY_BACKSPACE:      // '\b' = 0x08
            if (Handle.len)      // 缓冲区非空才能删
            {
                /* 缓冲区不是空的，光标左移，删除最后一个字符 */
                TERMINAL_CLEAR_END();   // 发 ESC[K 让终端清除光标后内容
                                        // 【说明】终端软件在你按退格时已经把光标左移了，
                                        //   这里只需要清掉光标右边的残留字符
                /* 开始删除字符 */
                Handle.len--;    // 缓冲区长度减1（逻辑删除，内容不用真的清）
            }
            break;

        // ---- 情况2：收到回车键 ----
        case KEY_ENTER:          // '\r' = 0x0D
            /* 只输入了一个回车键 */
            if (!Handle.len)     // 空行，什么都没输入
            {
                Handle.len = 0;
                PRINTF("\r\n-> console:");   // 只打印新的提示符
                break;
            }
            /* 处理解析函数 */
            goto deal;           // 【goto 跳出双层结构】
                                 //   从 switch 内部直接跳出 while 循环去执行命令
                                 //   这里用 goto 是合理的（跳出多层嵌套的经典场景）

        // ---- 情况3：普通字符 ----
        default:
            Handle.buff[Handle.len++] = data;   // 存入缓冲，长度+1
            break;
            // 【注意】这里没有回显！用户输入的字符不会显示在终端上。
            //   实际使用时依赖终端软件开启"本地回显(Local Echo)"功能。
        }
    }

    return;    // 队列取空，本次 console_run 结束，返回主循环

// ============ 命令执行区 ============
deal:
    Handle.buff[Handle.len] = '\0';   // 给命令行字符串加结束符
    char *argv[MAX_ARGS];             // 参数指针数组

    /* 解析命令
     *    console_split_argv
     *	  返回解析的参数个数，入参:要解析的字符串，存储解析参数的区域，最大解析参数个数
     *
     * -> console:set -i 192.168.1.66 -o test
     *		Parsed 5 arguments:
     *		argv[0]: set
     *		argv[1]: -i
     *		argv[2]: 192.168.1.66
     *		argv[3]: -o
     *		argv[4]: test
     */
    size_t argc = console_split_argv((char *)Handle.buff, argv, MAX_ARGS);

    /* 调用链表寻找解析函数处理 */
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {   // 遍历所有已注册的命令
        if (strcmp(pos->command, argv[0]) == 0)   // 命令名精确匹配
        {                                         // argv[0] 是用户输入的第一个词
            /* 调用回调函数 */
            if (!pos->func(argc, argv))    // 【执行命令】把 argc/argv 传进去
            {                              // 返回 0 = 失败
                PRINTF("\r\n-> PARAME. ERR\r\n");   // 提示参数错误
            }
            goto clear;    // 找到并执行了，跳到清理区
        }
    }

    // 【走到这里说明遍历完了都没匹配上】
    PRINTF("\r\n-> CMD ERR, try: help\r\n\r\n");

clear:
    PRINTF("\r\n-> console:");         // 打印新提示符
    memset(Handle.buff, 0, BUFFER_SIZE);   // 清空输入缓冲
    Handle.len = 0;                        // 长度归零，准备接收下一条命令
}
```

**函数2：`clear` 命令 —— 清屏**

```c
static int console_clear(int argc, char **argv)
{
    TERMINAL_RESET_CURSOR();      // ESC[H 光标回左上角
    TERMINAL_DISPLAY_CLEAR();     // ESC[2J 清空屏幕
    return true;                  // 返回 1（stdbool 的 true）
}
```

**函数3：`show` 命令 —— 显示测试统计**

```c
static int console_show(int argc, char **argv)
{
    PRINTF("\r\n");
    cmd_item_t *pos, *ppos;
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {
        if (strlen(pos->command) > 3)
        {
            // 过滤掉一些系统命令
            continue;
            // 【巧妙的过滤技巧】
            //   传感器命令是 "1"~"8"，长度都是 1
            //   系统命令是 "clear"(5) "help"(4) "show"(4) "auto"(4)，长度都 >3
            //   所以用长度就能区分，不需要额外加标志位字段
        }
        // 打印格式：命令 帮助文本  成功次数  失败次数
        PRINTF("%s %s         success %d  fail  %d\r\n",
               pos->command, pos->help, pos->success, pos->fail);
    }
    return true;
}
```

**函数4：`auto` 命令 —— 一键全自动测试（产线核心功能）**

```c
static int console_test(int argc, char **argv)
{
    cmd_item_t *pos, *ppos;
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {
        if (strlen(pos->command) > 3)
        {
            // 过滤掉一些系统命令（避免递归调用自己！）
            continue;
            // 【关键】如果不过滤，遍历到 "auto" 自己时会无限递归导致栈溢出
        }

        if (pos->func(argc, argv))    // 依次执行 "1"~"8" 每个测试命令
        {
            pos->success++;           // 通过 → 成功计数+1
        }
        else
        {
            pos->fail++;              // 失败 → 失败计数+1
        }
    }
    return true;
}
// 【产线使用流程】
//   1. 插上板子，打开串口终端
//   2. 敲 auto 回车 → 自动跑完 8 项测试，屏幕滚动打印每项 PASS/FAIL
//   3. 敲 show 回车 → 汇总看每项的成功/失败次数
//   4. 全 PASS → 贴合格标签；有 FAIL → 返修
// 【计数器溢出】success/fail 是 unsigned char，最大 255，
//   连续测 256 次会回绕到 0。产线测试不会跑这么多次，可接受。
```

**函数5：`help` 命令**

```c
static int console_help(int argc, char **argv)
{
    cmd_item_t *pos, *ppos;
    list_for_each_entry_safe(pos, ppos, &cmd_list, list)
    {   // 不过滤，显示所有命令（包括系统命令）
        PRINTF("\r\n-> CMD[%s] %s\r\n", pos->command, pos->help);
    }
    return 1;
}
```

**函数6：`console_cmd_register()` —— 命令注册（框架的关键）**

```c
/* 注册命令到系统链表中 */
void console_cmd_register(cmd_item_t *cmd)
{
    cmd_item_t *node;
    if (!cmd)      // 空指针保护
    {
        printf("cmd is error\n");
        return;
    }

    node = (cmd_item_t *)malloc(sizeof(cmd_item_t));
    // 【为什么要 malloc？】
    //   调用方传进来的 cmd 通常是【栈上的局部变量】（看各驱动的 xxx_cmd_register）
    //   函数返回后栈就销毁了，指针会失效
    //   所以必须在堆上申请一块永久内存，把内容拷贝过来
    // 【隐患1】没有检查 malloc 返回 NULL（堆耗尽时会崩溃）
    // 【隐患2】node->success 和 node->fail 没有初始化！
    //   malloc 不清零，它们是随机的堆垃圾值。
    //   所以第一次 show 可能显示奇怪的数字。
    //   （虽然调用方的结构体里写了 .success=0，但这里没拷贝这两个字段）

    strcpy(node->command, (const char *)cmd->command);   // 拷命令名
    strcpy(node->help, (const char *)cmd->help);         // 拷帮助文本
    node->func = cmd->func;                              // 拷函数指针

    list_add_tail(&node->list, &cmd_list);   // 【挂到链表尾部】
                                             // 注册顺序 = 链表顺序 = auto 测试顺序
}
```

**函数7：系统命令注册**

```c
static void system_cmd_register(void)
{
    // 每个命令都用同样的模式：定义局部 const 结构体 → 注册
    const cmd_item_t clearcmd = {
        .command = "clear",
        .help = "\r\n* Clear the screen\r\n",
        .func = &console_clear,
    };
    console_cmd_register((cmd_item_t *)&clearcmd);
    // 强制去掉 const 是因为 console_cmd_register 的参数不是 const 指针
    // （函数内部只读不写，所以安全）

    const cmd_item_t helpcmd = {
        .command = "help",
        .help = "\r\n* Show all commands\r\n",
        .func = &console_help,
    };
    console_cmd_register((cmd_item_t *)&helpcmd);

    const cmd_item_t showcmd = {
        .command = "show",
        .help = "\r\n* 显示测试结果\r\n",
        .func = &console_show,
    };
    console_cmd_register((cmd_item_t *)&showcmd);

    const cmd_item_t testcmd = {
        .command = "auto",
        .help = "\r\n* 自动测试 \r\n",
        .func = &console_test,
    };
    console_cmd_register((cmd_item_t *)&testcmd);
}
```

**函数8：`console_init()` —— 控制台初始化**

```c
void console_init(void)
{
    INIT_LIST_HEAD(&cmd_list);    // 【必须第一步】初始化链表头，
                                  //   让 next/prev 都指向自己（空循环链表）
                                  //   否则 list_add_tail 会操作野指针崩溃
    system_cmd_register();        // 注册 4 个系统命令

    TERMINAL_DISPLAY_CLEAR();     // 清屏
    TERMINAL_RESET_CURSOR();      // 光标归位

    PRINTF("-------------------------------\r\n\r\n");
    TERMINAL_HIGH_LIGHT();        // 开启反显（黑底白字变白底黑字）
    INFO("    Console version: V1.0          \r\n\r\n");
    INFO("    coder:  XS                 	   \r\n\r\n");
    TERMINAL_UN_HIGH_LIGHT();     // 关闭反显
    PRINTF("-------------------------------\r\n\r\n");
    PRINTF("\r\n-> console:");    // 打印命令提示符，等待用户输入
}
```

### 3.13.3 控制台完整数据流

```
用户在终端敲 "1" + 回车
        ↓
USART1 硬件收到 '1' (0x31) → 触发 RXNE 中断
        ↓
USART1_IRQHandler() → HAL_UART_IRQHandler()
        ↓
HAL 把字节存入 dataRcvd，调用 HAL_UART_RxCpltCallback()
        ↓
main.c 回调：queue_in(&dataRcvd)  → 字符进环形队列
             HAL_UART_Receive_IT(...) → 续期，准备收下一个
        ↓
【异步分界线】—— 中断结束，回到主循环
        ↓
while(1) → console_run()
        ↓
queue_out(&data) 取出 '1' → 不是回车不是退格 → Handle.buff[0]='1', len=1
queue_out(&data) 取出 '\r' → 是回车 → goto deal
        ↓
console_split_argv("1", argv, 6) → argc=1, argv[0]="1"
        ↓
遍历 cmd_list：clear? no  help? no  show? no  auto? no
              "1"? YES! → 调用 bh7150_read(1, argv)
        ↓
bh7150_read 执行 I2C 读取 → printf 打印结果（又走 fputc → USART1 发出去）
        ↓
返回 1 → 不打印 PARAME.ERR → goto clear
        ↓
打印 "-> console:" 提示符，清缓冲，等下一条命令
```

---

# 第四部分 · `main.c` 用户业务逻辑详解

## 4.1 全局变量与配置参数

```c
/* USER CODE BEGIN PV */
static uint32_t collect_tick = 0;    // 【未使用】遗留变量
/* USER CODE END PV */

/* USER CODE BEGIN PFP */
bool key_1_touch = false;    // KEY1 按下事件标志（中断置位，key_function 清零）
bool key_2_touch = false;    // KEY2 按下时的事件标志，在中断里置 true，key_function 中清零
bool key_3_touch = false;    // KEY3 按下事件标志（置了但没人用）
bool fs_status = false;      // "蜂鸣器/全亮"手动模式状态（KEY3 切换）
                             // csb.c 里 extern 它，用于避免自动/手动控制冲突
bool lcd_status = false;     // LCD 状态标志，在 page1 上显示 open/close
int now_page = 0;            // LCD 当前显示页面索引 0 或 1

// ============ 【核心可调参数】各传感器的采集周期（单位：秒） ============
#define BH1750_TIMER_PERIOD   3    // 光照每 3 秒采一次
#define DHT11_TIMER_PERIOD    10   // 温湿度每 10 秒采一次（DHT11 最快 1 秒，取 10 秒稳妥）
#define SCD04_TIMER_PERIOD    5    // CO₂ 采集命令后等 5 秒（对应传感器 5000ms 转换时间）
#define RFID_TIMER_PERIOD     2    // RFID 周期（本项目没有 RFID 模块，遗留定义）
#define CO2_READ_TIMER_PERIOD 1    // CO₂ 读数据后 1 秒再开始下一轮采集

// 【软件分频计数器】TIM2 每秒让它们各减 1，减到 <0 就触发对应任务
static int bh1750_count_timer = BH1750_TIMER_PERIOD;
static int dht11_count_timer  = DHT11_TIMER_PERIOD;
static int scd04_count_timer  = SCD04_TIMER_PERIOD;
static int rfid_count_timer   = RFID_TIMER_PERIOD;

// CO₂ 两阶段状态机的状态定义
enum
{
    COLLECT = 0,    // 阶段0：该发采集命令了
    READ = 1,       // 阶段1：该读数据了
};

static int co2_status = COLLECT;    // 当前状态，初始为"该采集"
/* USER CODE END PFP */
```

> **这是整个项目最重要的"软件定时器"设计**：只用一个硬件定时器（TIM2，1秒中断），通过给每个任务配一个整数计数器，实现了任意多个不同周期的软件定时任务。这是资源受限系统的标准做法，比开 4 个硬件定时器省得多。

## 4.2 TIM2 定时器中断回调

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    unsigned int retry = 0;      // 【未使用】遗留变量

    if (htim == &htim2)          // 判断是哪个定时器触发的
    {                            // 因为所有定时器共用这一个回调函数
        bh1750_count_timer--;    // 每秒所有计数器各减 1
        dht11_count_timer--;
        scd04_count_timer--;
        rfid_count_timer--;
    }
}
// 【设计精髓】中断里只做最简单的减法（几个时钟周期就完成），
//   真正耗时的采集动作放到主循环的 timer_collect_data() 里做。
//   这叫"中断置标志，主循环干活"，是嵌入式的黄金准则。
```

## 4.3 串口接收中断回调（6 路分发）

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    // ============ USART1：调试控制台 ============
    if (huart == &huart1) // 串口1
    {
        queue_in((char *)&dataRcvd);              // 字节进环形队列
        HAL_UART_Receive_IT(&huart1, &dataRcvd, 1);   // 续期
        return;
    }

    // ============ USART3：LCD 屏（定长 4 字节回应）============
    if (huart == &huart3) // LCD
    {
        lcd_buffer[lcd_count++] = lcd_temp;
        if (lcd_count == LCD_REPLAY_LEN)   // 收满 4 字节 "OK\r\n"
        {
            lcd_check_frame(); // 查看LCD是否准备就绪
        }
        HAL_UART_Receive_IT(&huart3, &lcd_temp, 1);
        return;
    }

    // ============ UART4：GNSS（帧头 '$' + 帧尾 '\r'）============
    if (huart == &huart4)
    {
        if (gnss_temp == 0x24)         // '$' 起始符
        {
            gnss_frame_ready = true;
        }

        if (gnss_frame_ready)
        {
            gnss_buffer[gnss_count++] = gnss_temp;
            if (gnss_count == GNSS_DATA_HEAD_LEN)   // 收到第 7 字节，校验帧头
            {
                if (!gnss_check_frame())
                {
                    gnss_count = 0;             // 不要这一帧，重置
                    gnss_frame_ready = false;
                }
            }
            else if (gnss_temp == 0x0D)   // '\r' 帧尾
            {
                gnss_deal_buffer();       // 一帧完成
            }
        }

        HAL_UART_Receive_IT(&huart4, &gnss_temp, 1);
        return;
    }

    // ============ UART5：超声波（帧头 0xFF + 定长 4 字节）============
    if (huart == &huart5)
    {
        if (csb_temp == 0xFF)
        {
            csb_frame_ready = true;
        }

        if (csb_frame_ready)
        {
            csb_buffer[csb_count++] = csb_temp;
            if (csb_count == CSB_FRAME_LEN)   // 满 4 字节
            {
                csb_deal_buffer();   // 解析距离 + 自动控制 LED
            }
        }

        HAL_UART_Receive_IT(&huart5, &csb_temp, 1);
        return;
    }

    // ============ UART7：星闪模块（无协议，全收）============
    if (huart == &huart7)
    {
        xs_buffer[xs_count++] = xs_temp;
        HAL_UART_Receive_IT(&huart7, &xs_temp, 1);
        return;
    }

    // ============ USART10：BMP280（帧头 'P' + 帧尾 '\r'）============
    if (huart == &huart10)
    {
        if (bmp280_temp == 0x50)      // 'P' = "Pressure" 首字母
        {
            bmp280_frame_ready = true;
        }

        if (bmp280_frame_ready)
        {
            bmp280_buffer[bmp280_count++] = bmp280_temp;
            if (bmp280_temp == 0x0D)   // '\r'
            {
                bmp280_deal_buffer();  // 解析气压和高度
            }
        }

        HAL_UART_Receive_IT(&huart10, &bmp280_temp, 1);
        return;
    }
}
```

### 三种串口协议解析模式对比

| 模式 | 使用者 | 起始判断 | 结束判断 | 优缺点 |
|---|---|---|---|---|
| **定长帧** | LCD, 超声波 | 帧头字节 / 无 | 计数达到固定长度 | 简单快，但丢一个字节就会永久错位 |
| **帧头+帧尾** | GNSS, BMP280 | 特征字符 | 遇到 `\r` | 可自恢复，适合 ASCII 文本 |
| **无协议全收** | 星闪 | 无 | 无 | 最简单，但有溢出风险 |

## 4.4 外部中断（按键）回调

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t key3_tick = 0;    // 【未使用】本想做软件消抖的，没实现
    static bool touch_3 = true;       // 【静态变量】KEY3 的翻转状态记忆
                                      // true = 下次按下要"开灯"，false = 下次要"关灯"

    switch (GPIO_Pin)     // 参数是触发中断的引脚号，三个键共用 EXTI15_10
    {
    // ============ KEY3：一键全亮/全灭 + 蜂鸣器 ============
    case KEY3_Pin:        // PE13
        if (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == GPIO_PIN_RESET)
        {   // 【二次确认】中断触发后再读一次引脚电平
            //   目的：过滤掉抖动产生的毛刺中断（如果是毛刺，此刻已经恢复高电平了）
            //   这是最简单的硬件消抖方式

            // KEY3 松开逻辑,上升沿触发
            key_3_touch = true;

            if (touch_3)     // 当前该"开"
            {
                HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_SET);  // 黄灯亮
                HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_SET);    // 绿灯亮
                HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_SET);        // 红灯亮
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 99);
                // 【PWM 控制】把 TIM3 通道3 的比较值设为 99
                //   ARR=99，所以 CCR=99 → 占空比 99/100 = 99% → 蜂鸣器最响
                //   宏展开：htim3.Instance->CCR3 = 99
                fs_status = true;      // 标记进入"手动模式"
                                       // → csb.c 看到这个标志就不再自动控制 LED
                lcd_status = true;     // LCD page1 上会显示 "open"
            }
            else             // 当前该"关"
            {
                HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_RESET);
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);   // 占空比 0 = 静音
                fs_status = false;     // 退出手动模式，超声波恢复自动控灯
                lcd_status = false;
            }
            touch_3 = !touch_3;    // 【状态翻转】下次按下执行相反的动作
        }
        break;

    // ============ KEY2：LCD 下一页 ============
    case KEY2_Pin:        // PE14
        // 读取引脚确认是否按下(低电平)，消抖并确认硬件状态
        if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET)
        {
            // KEY2 按下逻辑：仅置事件标志，真正的页面切换在 key_function() 中执行
            key_2_touch = true;
            // 【为什么不直接翻页？】因为 lcd_switch() 里有 HAL_Delay(500) 和
            //   HAL_UART_Transmit()，在中断里执行会阻塞 500ms+，
            //   期间所有低优先级中断（串口接收）都会被延迟甚至丢数据。
            //   所以只置标志，把耗时操作留给主循环。—— 这是本项目的正确示范！
        }
        break;

    // ============ KEY1：LCD 上一页 ============
    case KEY1_Pin:        // PE15
        if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET)
        {
            key_1_touch = true;
        }
        break;
    }
}
```

> **注意 KEY3 的反例**：KEY3 分支里直接操作了 GPIO 和 PWM 寄存器。这**恰好没问题**，因为 `HAL_GPIO_WritePin` 和 `__HAL_TIM_SET_COMPARE` 都是**单次寄存器写入**，只需几个 CPU 周期，不会阻塞。而 KEY1/KEY2 涉及串口发送和延时，就必须延后到主循环。**判断标准：中断里只能做"纳秒/微秒级"的事**。

## 4.5 LCD 页面渲染函数

```c
void lcd_set_page0(void)
{
    char temp[256] = {0};
    // 终端协议：SET_TXT(行号,'内容');  显示温湿度与光照
    // sprintf(temp, "SET_TXT(4,'10.5\xA1\xE3\x43');SET_TXT(5,'10.6RH');SET_TXT(6,'888lx');\r\n");
    sprintf(temp, "SET_TXT(4,'%.1f\xA1\xE3\x43');SET_TXT(5,'%.1fRH');SET_TXT(6,'%dlx');\r\n",
            tem,        // 【来自 DHT11.c】温度，%.1f 保留 1 位小数
            hum,        // 【来自 DHT11.c】湿度
            lx_Data);   // 【来自 BH1750.c】光照
    // 生成的字符串示例：
    //   SET_TXT(4,'25.0°C');SET_TXT(5,'55.0RH');SET_TXT(6,'600lx');\r\n
    // 三条指令用分号连在一起一次性发出，效率更高
    // 控件 ID 4/5/6 对应屏上组态设计好的三个文本框

    lcd_set_data(temp); // 通过 USART3 下发本页文本
}

void lcd_set_page1(void)
{
    char temp[256] = {0};
    sprintf(temp, "SET_TXT(4,'%s');SET_TXT(5,'%s');SET_TXT(6,'%ldPa');SET_TXT(7,'%ldPPM');\r\n",
            lcd_status > 0 ? "open" : "close",   // LCD 状态（KEY3 控制）
            fs_status > 0 ? "open" : "close",    // 蜂鸣器/灯状态
            pressure_Data,                        // 【来自 bmp280.c】气压 Pa
            data_co2);                            // 【来自 SCD04.c】CO₂ ppm
    // 【格式符不匹配警告】
    //   %ld 需要 long，但 pressure_Data 是 int，data_co2 是 uint16_t
    //   在 32 位 ARM 上 int 和 long 都是 4 字节，所以 pressure_Data 没问题
    //   但 data_co2 是 uint16_t，被可变参数提升为 int（4字节），%ld 读 4 字节，
    //   恰好也对，所以实际能正常工作 —— 但这是"运气好"，不是好代码

    lcd_set_data(temp);
}
```

## 4.6 按键任务处理（主循环调用）

```c
void key_function(void)
{
    const int PAGE_COUNT = 2; /* 页面总数，用于循环切换 */

    /* KEY1 处理：切换到上一页 */
    if (key_1_touch)      // 检查中断置的标志
    {
        int target = (now_page - 1 + PAGE_COUNT) % PAGE_COUNT; /* 计算目标页面（循环） */
        // 【环形翻页公式】
        //   为什么要 +PAGE_COUNT？因为 C 语言里 (-1) % 2 = -1（不是 1）
        //   加上 PAGE_COUNT 保证被除数为正：
        //     now_page=0 → (0-1+2)%2 = 1%2 = 1  （第0页往前翻到第1页）
        //     now_page=1 → (1-1+2)%2 = 2%2 = 0  （第1页往前翻到第0页）

        lcd_switch(target);       // 发 JUMP 指令切页面

        if (target)               // target=1
        {
            lcd_set_page1();      // 填充第1页内容
            now_page = 1;
        }
        else                      // target=0
        {
            lcd_set_page0();
            now_page = 0;
        }
        key_1_touch = false; /* 清除标志 */
        // 【竞态风险】如果在 lcd_switch 的 500ms 延时期间用户又按了一次键，
        //   中断会再置 true，但这里会立刻清掉 → 丢失一次按键。
        //   对人机交互场景可接受（人手速没那么快）。
    }

    /* KEY2 处理：切换到下一页 */
    if (key_2_touch)
    {
        int target = (now_page + 1) % PAGE_COUNT; /* 计算目标页面（循环） */
        //   now_page=0 → (0+1)%2 = 1
        //   now_page=1 → (1+1)%2 = 0
        lcd_switch(target);
        if (target)
        {
            lcd_set_page1();
            now_page = 1;
        }
        else
        {
            lcd_set_page0();
            now_page = 0;
        }
        key_2_touch = false; /* 清除标志 */
    }
}
```

## 4.7 定时采集任务（最核心的调度逻辑）

```c
static void timer_collect_data(void)
{
    // ============ 任务1：DHT11 温湿度（10 秒周期，优先级最高）============
    if (dht11_count_timer < 0)      // 计数器减到负数 = 到期
    {
        HAL_TIM_Base_Stop_IT(&htim2);    // 【关键】先停掉 TIM2 中断！
        // 【为什么要停？】
        //   DHT11_Data() 内部是微秒级严格时序，如果被 TIM2 中断打断，
        //   Delay_us(50) 的采样点就会错过，读到乱码。
        //   停定时器 = 临时屏蔽干扰源。
        //   （比 __disable_irq() 温和：串口中断还能正常工作）

        DHT11_Data(&tem, &hum);          // 采集（约耗时 25ms：20ms起始 + 40bit数据）
        dht11_count_timer = DHT11_TIMER_PERIOD;   // 重装计数器 = 10
        goto end;                        // 跳到统一的收尾处理
        // 【goto 的作用】保证每轮最多只执行一个采集任务，
        //   避免多个任务同时到期时在一次主循环里连续阻塞太久
    }

    // ============ 任务2：SCD41 CO₂（两阶段状态机）============
    if (scd04_count_timer < 0)
    {
        HAL_TIM_Base_Stop_IT(&htim2);

        if (co2_status == COLLECT)       // 阶段A：发起采集
        {
            scd04_collect();                          // 只发命令，立即返回（不阻塞！）
            scd04_count_timer = SCD04_TIMER_PERIOD;   // 设 5 秒
                                                      // 对应传感器 5000ms 转换时间
            co2_status = READ;                        // 下次进来执行阶段B
        }
        else                             // 阶段B：读取结果
        {
            co2_status = COLLECT;                       // 下次回到阶段A
            scd04_read_data();                          // 读 9 字节（只阻塞 ~1ms）
            scd04_count_timer = CO2_READ_TIMER_PERIOD;  // 设 1 秒后开始下一轮采集
        }
        goto end;
    }
    // ============ CO₂ 状态机时间轴 ============
    //   t=0s   : COLLECT → 发 measure_single_shot，计数器=5，状态→READ
    //   t=1~4s : 计数器递减，CPU 自由处理其他任务 ← 【这就是拆两步的价值】
    //   t=5s   : READ → 读数据，计数器=1，状态→COLLECT
    //   t=6s   : COLLECT → 又发采集命令...
    //   完整周期 = 6 秒，其中只有约 1ms 是阻塞的
    //   对比 scd04_get_Data()：一次调用阻塞 5001ms！

    // ============ 任务3：BH1750 光照（3 秒周期，优先级最低）============
    if (bh1750_count_timer < 0)
    {
        HAL_TIM_Base_Stop_IT(&htim2);
        bh1750_count_timer = BH1750_TIMER_PERIOD;   // 重装 = 3
        bh7150_get_data();                          // I2C 读 2 字节，约 0.2ms
        goto end;
    }

    return;      // 没有任务到期，直接返回

end:
    // ============ 统一收尾：刷新 LCD + 恢复定时器 ============
    if (now_page)          // 当前在第 1 页
    {
        lcd_set_page1();   // 刷新第1页数据
    }
    else                   // 当前在第 0 页
    {
        lcd_set_page0();
    }
    HAL_TIM_Base_Start_IT(&htim2);   // 【必须】重新启动 TIM2 中断
                                     // 否则计数器不再递减，所有定时任务永久停止！
}
```

### 优先级设计分析

因为用了 `goto end`，判断顺序就是优先级顺序：

```
优先级1（最高）：DHT11  —— 因为它对时序最敏感，必须优先保证
优先级2        ：SCD41  —— CO₂ 状态机不能被打乱节奏
优先级3（最低）：BH1750 —— I2C 读取快，随时可以做，等一等无所谓
```

## 4.8 开机首次采集

```c
static void get_sensor_data(void)
{
    // 先采集二氧化碳，这个传感器有延时，这样其他传感器有一个初始化的时间
    HAL_Delay(3000);      // 【等 3 秒】
    // 【为什么要等？】
    //   1. DHT11 上电后需要 ≥1 秒才能稳定（数据手册要求）
    //   2. BH1750 连续模式第一次转换需要 ~120ms
    //   3. SCD41 上电自检需要时间
    //   统一等 3 秒，简单粗暴但有效

    scd04_get_Data();     // 【阻塞版】发采集 + 等5秒 + 读数据
                          //   开机时用阻塞版没问题，反正也在初始化阶段
                          //   而且这 5 秒也顺便给其他传感器充分预热时间
    DHT11_Data(&tem, &hum);   // 采温湿度
    bh7150_get_data();        // 采光照
}
// 总耗时约 3 + 5 + 0.03 + 0.001 ≈ 8 秒
// 所以这块板子上电后要等约 8 秒 LCD 才会显示第一屏数据
```

## 4.9 `main()` 函数完整流程

```c
int main(void)
{
    /* ---- MCU 基础配置（CubeMX 生成）---- */
    HAL_Init();                  // HAL 库初始化：Flash 预取、NVIC 分组、SysTick 1ms
    SystemClock_Config();        // 时钟树：HSI 64MHz 直驱

    /* ---- 外设初始化（CubeMX 生成）---- */
    MX_GPIO_Init();              // 【必须第一个】其他外设的引脚都依赖 GPIO 时钟
    MX_TIM1_Init();              // 微秒延时基准
    MX_USART1_UART_Init();       // 调试串口
    MX_USART3_UART_Init();       // LCD
    MX_I2C3_Init();              // BH1750
    MX_USART10_UART_Init();      // BMP280
    MX_I2C2_Init();              // SCD41
    MX_TIM3_Init();              // PWM 蜂鸣器
    MX_UART4_Init();             // GNSS
    MX_UART5_Init();             // 超声波
    MX_UART7_Init();             // 星闪
    MX_TIM2_Init();              // 1 秒节拍

    /* USER CODE BEGIN 2 —— 【用户手写初始化区】 */

    // ① PWM 启动但静音
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);          // 启动 PWM 输出
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);   // 占空比设 0 → 蜂鸣器不响
    // 【为什么不直接不启动？】因为 KEY3 中断里只改 CCR 不启动 PWM，
    //   如果这里不 Start，按 KEY3 也不会响

    // ② 传感器初始化
    SCD04_INIT();      // 只填结构体（I2C2 + 地址 0x62）
    BH1750_INIT();     // 填结构体 + 发 POWER_ON/RESET/H_RES_MODE 三条指令

    // ③ 【关键】启动 6 路串口的接收中断
    HAL_UART_Receive_IT(&huart1, &dataRcvd, 1);      // 控制台
    HAL_UART_Receive_IT(&huart3, &lcd_temp, 1);      // LCD
    HAL_UART_Receive_IT(&huart4, &gnss_temp, 1);     // GNSS
    HAL_UART_Receive_IT(&huart5, &csb_temp, 1);      // 超声波
    HAL_UART_Receive_IT(&huart7, &xs_temp, 1);       // 星闪
    HAL_UART_Receive_IT(&huart10, &bmp280_temp, 1);  // BMP280
    // 每个都是"收 1 字节就中断"，在回调里自我续期

    // ④ 启动 1 秒节拍定时器
    HAL_TIM_Base_Start_IT(&htim2);

    // ⑤ 控制台初始化（建链表 + 注册 clear/help/show/auto + 打欢迎屏）
    console_init();

    // ⑥ 注册 8 个传感器测试命令（注册顺序 = auto 测试顺序）
    BH7150_cmd_register();   // "1" 光照
    bmp280_cmd_register();   // "2" 大气压
    csb_cmd_register();      // "3" 超声波
    dht11_cmd_register();    // "4" 温湿度
    xs_cmd_register();       // "5" 星闪
    gnss_cmd_register();     // "6" GNSS
    lcd_cmd_register();      // "7" LCD
    SCD04_cmd_register();    // "8" CO₂

    // ⑦ 首次采集（约 8 秒）
    get_sensor_data();

    // ⑧ 显示第 0 页
    lcd_set_page0();

    /* USER CODE END 2 */

    /* ============ 主循环 ============ */
    while (1)
    {
        console_run();          // ① 处理串口命令行输入
        key_function();         // ② 处理按键翻页
        timer_collect_data();   // ③ 检查定时器，到期就采集+刷屏
    }
}
```

### 主循环设计评价

这是一个典型的 **"超级循环 + 前后台系统"（Super Loop / Foreground-Background）** 架构：

- **后台（Background）**= `while(1)` 里的三个函数，顺序轮询执行
- **前台（Foreground）**= 中断服务程序，异步抢占

**优点**：
- 无需 RTOS，代码简单，RAM 占用小
- 三个任务互不阻塞（每个都是"检查标志→有事才做"）

**缺点**：
- 响应时间不确定。如果 `timer_collect_data()` 正在跑 DHT11（25ms），此时用户敲命令，命令要等 25ms 后才处理
- `lcd_switch()` 里的 `HAL_Delay(500)` 会让整个主循环停摆 500ms

**改进方向**（如果要升级）：把 `HAL_Delay` 改成非阻塞的 `HAL_GetTick()` 时间戳判断，或者上 FreeRTOS。

---

# 第五部分 · 文件串联关系与完整调用链

## 5.1 依赖关系图

```
                          ┌──────────┐
                          │  main.c  │ ← 应用层：调度、中断分发、页面逻辑
                          └────┬─────┘
             ┌─────────────────┼─────────────────┐
             ↓                 ↓                 ↓
      ┌────────────┐    ┌────────────┐    ┌──────────┐
      │ console.c  │    │ 8个驱动文件 │    │  lcd.c   │
      │ 命令行框架  │←───│ 注册命令    │    │ 屏幕输出  │
      └─────┬──────┘    └─────┬──────┘    └────┬─────┘
            │                  │                │
    ┌───────┼──────┐          │                │
    ↓       ↓      ↓          ↓                ↓
┌───────┐┌──────┐┌────────┐ ┌────┐        ┌────────┐
│queue.c││list.h││split_  │ │HAL │        │HAL_UART│
│环形队列││链表  ││argv.c  │ │I2C │        │Transmit│
└───┬───┘└──────┘└────────┘ │UART│        └────────┘
    │                        │GPIO│
    ↓                        │TIM │
┌─────────┐                 └────┘
│usr_uart │
│printf   │
└─────────┘
```

## 5.2 六条完整调用链

### 链路 1：用户敲命令测试光照传感器

```
【硬件】用户在串口终端敲 "1" + Enter
   ↓
【中断】USART1 RXNE 中断 → USART1_IRQHandler() [stm32h7xx_it.c]
   ↓
        HAL_UART_IRQHandler(&huart1) [HAL库]
   ↓
        HAL_UART_RxCpltCallback(&huart1) [main.c:104]
   ↓
        queue_in((char*)&dataRcvd) [queue.c:21]
              → cirbuffer.buffer[head++] = '1'
   ↓
        HAL_UART_Receive_IT(&huart1, &dataRcvd, 1) 续期
   ↓ ── 中断结束，异步分界 ──
【主循环】while(1) → console_run() [console.c:18]
   ↓
        queue_out(&data) [queue.c:36] → 取出 '1'
              → Handle.buff[0]='1', Handle.len=1
        queue_out(&data) → 取出 '\r' → case KEY_ENTER → goto deal
   ↓
        console_split_argv("1", argv, 6) [split_argv.c:38]
              → argc=1, argv[0]="1"
   ↓
        list_for_each_entry_safe 遍历 cmd_list [list.h]
              → strcmp("1", "1") == 0 命中！
   ↓
        pos->func(1, argv) → bh7150_read() [BH1750.c:115]
   ↓
        BH1750_get_lumen(&bh7150_dev) [BH1750.c:89]
   ↓
        HAL_I2C_Master_Receive(hi2c3, 0x47, buffer, 2, 10) [HAL库]
   ↓
        【I2C3 硬件】START | 0x47 | ACK | 数据高字节 | ACK | 数据低字节 | NACK | STOP
   ↓
        value = (buffer[0]<<8 | buffer[1]) / 1.2 → lx 值
   ↓
        lx_Data = value  【全局变量更新，LCD 后续会用到】
   ↓
        PRINTF("* lx: %ld") → fputc() [usr_uart.c:15]
              → UART_SenddBuf(&huart1,...) → HAL_UART_Transmit()
   ↓
【硬件】USART1 TX 输出 → 用户屏幕看到 "* lx: 600"
   ↓
        return 1 → console.c 不打印 PARAME.ERR → goto clear
   ↓
        PRINTF("-> console:") 新提示符
```

### 链路 2：自动定时采集 → LCD 刷新

```
【硬件】TIM2 计数溢出（每 1 秒）
   ↓
【中断】TIM2_IRQHandler() → HAL_TIM_IRQHandler(&htim2)
   
HAL_TIM_PeriodElapsedCallback(&htim2) [main.c:92]
   ↓
        bh1750_count_timer--  (3→2→1→0→-1)
        dht11_count_timer--
        scd04_count_timer--
   ↓ ── 中断结束 ──
【主循环】while(1) → timer_collect_data() [main.c:312]
   ↓
        检查 bh1750_count_timer < 0 → 成立
   ↓
        HAL_TIM_Base_Stop_IT(&htim2)   停节拍，防打扰
   ↓
        bh1750_count_timer = 3         重装
   ↓
        bh7150_get_data() [BH1750.c:108]
              → BH1750_get_lumen() → HAL_I2C_Master_Receive(hi2c3,...)
              → lx_Data = 计算结果
   ↓
        goto end
   ↓
        now_page == 0 → lcd_set_page0() [main.c:251]
   ↓
        sprintf(temp, "SET_TXT(4,'%.1f°C');SET_TXT(5,'%.1fRH');SET_TXT(6,'%dlx');\r\n",
                tem, hum, lx_Data)
              ← tem/hum 来自 DHT11.c，lx_Data 来自 BH1750.c
   ↓
        lcd_set_data(temp) [lcd.c:93]
              → if(!lcd_ready) HAL_Delay(500)   流控等待
              → HAL_UART_Transmit(&huart3, temp, ...)
              → lcd_ready = false   置忙
   ↓
【硬件】USART3 TX → LCD 屏收到指令，刷新显示
   ↓
【硬件】LCD 屏回 "OK\r\n" → USART3 RX 中断 ×4 次
   ↓
        HAL_UART_RxCpltCallback(&huart3) [main.c:113]
              → lcd_buffer[lcd_count++] = lcd_temp
              → lcd_count == 4 → lcd_check_frame() [lcd.c:19]
                    → memcmp(lcd_buffer,"OK\r\n",4)==0
                    → lcd_ready = true   【解除忙标志】
   ↓
        HAL_TIM_Base_Start_IT(&htim2)   恢复节拍
```

### 链路 3：超声波自动控灯（纯中断驱动，不经主循环）

```
【硬件】超声波模块每隔一段时间主动发 4 字节
   ↓
【中断】UART5 RX 中断 ×4 次
   ↓
        HAL_UART_RxCpltCallback(&huart5) [main.c:152]
   ↓
        第1字节 0xFF → csb_frame_ready = true
        csb_buffer[0]=0xFF, [1]=距离高, [2]=距离低, [3]=校验
   ↓
        csb_count == CSB_FRAME_LEN(4) → csb_deal_buffer() [csb.c:14]
   ↓
        distance = gn_buffer[1]<<8 | gn_buffer[2]
   ↓
        if(!fs_status)   ← 【跨文件互斥】读 main.c 的 KEY3 手动模式标志
              distance < 300 ? 三灯全亮 : 三灯全灭
   ↓
        HAL_GPIO_WritePin(GREEN/YELLOW/RED)
   ↓
【硬件】LED 亮灭
```

**这条链路完全在中断里完成，不依赖主循环**——因为动作只是几次寄存器写入，耗时可忽略。

### 链路 4：CO₂ 两阶段状态机（跨 6 秒的异步流程）

```
t=0s  【主循环】timer_collect_data()
         scd04_count_timer < 0 且 co2_status == COLLECT
      → HAL_TIM_Base_Stop_IT(&htim2)
      → scd04_collect() [SCD04.c:209]
            → scd04_send_command(dev, 0x219D, 2)
                  → data[0]=0x21, data[1]=0x9D  (大端拆分)
                  → HAL_I2C_Master_Transmit(hi2c2, 0xC4, data, 2, 10)
            【立即返回，不等待】
      → scd04_count_timer = 5
      → co2_status = READ
      → 刷 LCD → HAL_TIM_Base_Start_IT(&htim2)

t=1~4s 【TIM2 中断】scd04_count_timer 递减 5→4→3→2→1
       【主循环】CPU 正常处理命令行、按键、其他传感器
       【传感器内部】光声法测量进行中（需要 5000ms）

t=5s  【主循环】scd04_count_timer < 0 且 co2_status == READ
      → co2_status = COLLECT
      → scd04_read_data() [SCD04.c:221]
            → 发 0xEC05 (read_measurement)
            → HAL_Delay(1)
            → sensor_recv(dev, 9)
                  → HAL_I2C_Master_Receive(hi2c2, 0xC5, buffer, 9, 10)
            → data_co2 = buffer[0]<<8 | buffer[1]    【全局变量更新】
      → scd04_count_timer = 1
      → lcd_set_page1() 会用到 data_co2

t=6s  回到 t=0s，循环
```

### 链路 5：按键翻页（中断置标志 → 主循环执行）

```
【硬件】用户按下 KEY2 (PE14)，引脚下降沿
   ↓
【中断】EXTI15_10_IRQHandler() → HAL_GPIO_EXTI_IRQHandler(KEY2_Pin)
   ↓
        HAL_GPIO_EXTI_Callback(KEY2_Pin) [main.c:200]
   ↓
        HAL_GPIO_ReadPin(KEY2) == RESET  二次确认消抖
   ↓
        key_2_touch = true    【仅置标志，1个时钟周期】
   ↓ ── 中断结束 ──
【主循环】while(1) → key_function() [main.c:271]
   ↓
        if(key_2_touch) 成立
   ↓
        target = (now_page + 1) % 2   环形翻页
   ↓
        lcd_switch(target) [lcd.c:76]
              → if(!lcd_ready) HAL_Delay(500)
              → sprintf(temp, "JUMP(1);\r\n")
              → HAL_UART_Transmit(&huart3, ...)
              → lcd_ready = false
   ↓
        lcd_set_page1() → 填充该页数据
              → 读 lcd_status/fs_status/pressure_Data/data_co2
   ↓
        now_page = 1
        key_2_touch = false   清标志
```

### 链路 6：`auto` 一键全测（产线核心）

```
用户敲 "auto" + Enter
   ↓
console_run() → 匹配到 "auto" → console_test() [console.c:128]
   ↓
list_for_each_entry_safe 遍历 cmd_list:
   ├─ "clear"(5字符 >3) → continue 跳过
   ├─ "help"(4 >3)      → continue
   ├─ "show"(4 >3)      → continue
   ├─ "auto"(4 >3)      → continue  【防止递归自己】
   ├─ "1" → bh7150_read()   → I2C3 读 BH1750  → PASS/FAIL → success++/fail++
   ├─ "2" → bmp280_test()   → 打印缓存帧      → PASS/FAIL
   ├─ "3" → csb_test()      → 打印缓存距离    → PASS/FAIL
   ├─ "4" → DHT11_get()     → 关中断+单总线+等2秒 → PASS/FAIL
   ├─ "5" → xs_get_msg()    → 发AT+MAC?+等1秒  → PASS/FAIL
   ├─ "6" → gnss_test()     → 打印缓存NMEA    → PASS/FAIL
   ├─ "7" → lcd_test()      → 发指令+等2秒     → PASS/FAIL
   └─ "8" → scd04_test()    → I2C2 读序列号   → PASS/FAIL
   ↓
总耗时约 5~6 秒（主要是 DHT11 的 2s + 星闪 1s + LCD 2s）
   ↓
用户敲 "show" → console_show() → 打印每项的 success/fail 统计表
```

## 5.3 全局变量跨文件共享表

这是理解本项目"数据怎么流动"的关键——**各模块通过全局变量而非函数参数交换数据**：

| 全局变量 | 定义位置 | 类型 | 生产者（谁写） | 消费者（谁读） |
|---|---|---|---|---|
| `lx_Data` | BH1750.c | `uint16_t` | `bh7150_get_data()` | `lcd_set_page0()` |
| `tem` / `hum` | DHT11.c | `float` | `DHT11_Data()` | `lcd_set_page0()` |
| `data_co2` | SCD04.c | `uint16_t` | `scd04_read_data()` | `lcd_set_page1()` |
| `pressure_Data` | bmp280.c | `int` | `bmp280_deal_buffer()`（中断） | `lcd_set_page1()` |
| `height_Data` | bmp280.c | `double` | `bmp280_deal_buffer()` | 无人读（预留） |
| `fs_status` | main.c | `bool` | `HAL_GPIO_EXTI_Callback` (KEY3) | `csb_deal_buffer()`, `lcd_set_page1()` |
| `lcd_status` | main.c | `bool` | KEY3 中断 | `lcd_set_page1()` |
| `now_page` | main.c | `int` | `key_function()` | `timer_collect_data()` |
| `lcd_ready` | lcd.c | `bool` | `lcd_check_frame()`（中断） | `lcd_switch()`, `lcd_set_data()` |
| `key_1/2/3_touch` | main.c | `bool` | EXTI 中断 | `key_function()` |
| `dataRcvd` | usr_uart.c | `uint8_t` | HAL（USART1 中断） | `queue_in()` |
| `xxx_temp` / `xxx_buffer` / `xxx_count` | 各驱动 .c | — | HAL + 中断回调 | 各 `xxx_deal_buffer()` |

> **⚠️ 缺少 `volatile` 的隐患**：`lcd_ready`、`key_2_touch`、`data_co2` 等变量被**中断修改、主循环读取**，标准做法应加 `volatile` 关键字，否则编译器可能把它们缓存在寄存器里，导致主循环读到过时的值。本项目在 `-O0`/`-O1` 优化等级下能正常工作，但如果开 `-O2` 就可能出现"按键没反应""LCD 永远忙"等诡异现象。

## 5.4 分层调用总览

```
┌────────────────────────────────────────────────────────┐
│ 【应用层】main.c                                        │
│   while(1) { console_run(); key_function();            │
│              timer_collect_data(); }                   │
│   中断回调：TIM2 / 6×UART / EXTI                        │
└──────────┬──────────────────────┬──────────────────────┘
           ↓                      ↓
┌──────────────────┐   ┌─────────────────────────────────┐
│【控制台层】       │   │【驱动/模块层】                    │
│ console.c        │   │ I2C:  BH1750.c  SCD04.c         │
│  - 命令链表       │←──│ 单总线: DHT11.c                  │
│  - 命令分发       │注册│ 串口:  bmp280.c csb.c            │
│  - PASS/FAIL统计 │   │        gnss.c   EB21.c  lcd.c   │
└────┬─────────────┘   └──────────┬──────────────────────┘
     ↓                            ↓
┌───────────────────────┐  ┌──────────────────────────────┐
│【工具层】               │  │【通信层】                      │
│ queue.c   环形队列      │  │ usr_uart.c  printf→USART1    │
│ split_argv.c 分词状态机 │  │ HAL_UART_Receive_IT 自续期    │
│ list.h    侵入式链表    │  │                              │
└───────────────────────┘  └──────────────────────────────┘
                    ↓
┌────────────────────────────────────────────────────────┐
│ 【HAL 库层】stm32h7xx_hal_i2c/uart/gpio/tim/rcc.c        │
└────────────────────────────────────────────────────────┘
                    ↓
┌────────────────────────────────────────────────────────┐
│ 【硬件层】I2C2/I2C3, USART1/3/10, UART4/5/7,            │
│           TIM1/2/3, GPIOA~F, EXTI                      │
└────────────────────────────────────────────────────────┘
```

---

# 第六部分 · 从零手敲复刻完整指南

## 阶段一：CubeMX 工程创建与配置

### 步骤 1：新建工程、选芯片

1. 打开 **STM32CubeMX** → `File` → `New Project`
2. `MCU/MPU Selector` 搜索框输入你的具体型号（本项目为 STM32H7 系列，例如 `STM32H723VGT6` / `STM32H750VBT6`，请以实物丝印为准）
3. 双击选中 → 进入配置界面

### 步骤 2：配置时钟源（RCC）

1. 左侧 `System Core` → `RCC`
2. `High Speed Clock (HSE)` 选择 **`Disable`**（本项目不用外部晶振）
3. `Low Speed Clock (LSE)` 选择 **`Disable`**

### 步骤 3：配置时钟树（Clock Configuration 标签页）

| 配置项 | 设置值 |
|---|---|
| PLL Source Mux | 不使用（保持默认） |
| System Clock Mux | **HSI** |
| HSI 分频 | **/1**（得到 64MHz） |
| SYSCLK | 64 MHz |
| D1CPRE (SYSCLK Divider) | **/1** |
| HPRE (AHB Prescaler) | **/1** → HCLK = 64MHz |
| D2PPRE1 (APB1) | **/2** → 32MHz |
| D2PPRE2 (APB2) | **/2** → 32MHz |
| D1PPRE (APB3) | **/2** |
| D3PPRE (APB4) | **/2** |

在 `System Core` → `PWR` 中：
- `Power Regulator Voltage Scale` 选 **`Scale 3`**
- `Power Supply Source` 选 **`LDO`**

### 步骤 4：配置调试口

`System Core` → `SYS`：
- `Debug` 选 **`Serial Wire`**（SWD，两线调试，否则烧一次程序后就锁死了）
- `Timebase Source` 保持 **`SysTick`**

### 步骤 5：配置 6 路串口

在 `Connectivity` 中依次配置：

| 外设 | Mode | Baud Rate | Word Length | Parity | Stop Bits | NVIC 中断 |
|---|---|---|---|---|---|---|
| **USART1** | Asynchronous | **115200** | 8 Bits | None | 1 | ✅ 勾选 `USART1 global interrupt` |
| **USART3** | Asynchronous | **115200** | 8 Bits | None | 1 | ✅ 勾选 |
| **UART4** | Asynchronous | **115200** | 8 Bits | None | 1 | ✅ 勾选 |
| **UART5** | Asynchronous | **9600** ⚠️ | 8 Bits | None | 1 | ✅ 勾选 |
| **UART7** | Asynchronous | **115200** | 8 Bits | None | 1 | ✅ 勾选 |
| **USART10** | Asynchronous | **115200** | 8 Bits | None | 1 | ✅ 勾选 |

> 每个串口配置好后，切到该外设的 `NVIC Settings` 标签，**必须勾选 global interrupt**，否则 `HAL_UART_Receive_IT` 不会产生中断。

### 步骤 6：配置 2 路 I2C

| 外设 | Mode | Speed Mode | Frequency | 引脚 |
|---|---|---|---|---|
| **I2C2** | I2C | Standard Mode | **100 kHz** | 接 SCD41 |
| **I2C3** | I2C | Standard Mode | **100 kHz** | SCL=PA8, SDA=PC9，接 BH1750 |

配置后 CubeMX 会自动算出 `Timing = 0x00707CBB`。

> **硬件提醒**：I2C 的 SCL 和 SDA 必须外接 **4.7kΩ 上拉电阻到 3.3V**，否则通信失败。

### 步骤 7：配置 3 个定时器

**TIM1（微秒延时基准）：**
- `Clock Source` = **`Internal Clock`**
- `Prescaler` = **`64-1`**
- `Counter Mode` = `Up`
- `Counter Period (ARR)` = **`65535`**
- `auto-reload preload` = `Disable`
- **不要**开 NVIC 中断（纯查询使用）

**TIM2（1 秒系统节拍）：**
- `Clock Source` = **`Internal Clock`**
- `Prescaler` = **`6399`**
- `Counter Period` = **`9999`**
- `NVIC Settings` → ✅ 勾选 **`TIM2 global interrupt`**

**TIM3（PWM 蜂鸣器）：**
- `Clock Source` = `Internal Clock`
- `Channel3` = **`PWM Generation CH3`**
- `Prescaler` = **`64-1`**
- `Counter Period` = **`100-1`**
- `Pulse` = **`50`**
- 引脚映射到 **PB0**（在 Pinout 视图右键 PB0 选 `TIM3_CH3`）

### 步骤 8：配置 GPIO

在 **Pinout view** 中逐个点击引脚设置，然后在 `System Core` → `GPIO` 里配置属性并**打标签（User Label）**：

**输出引脚（LED 指示灯）：**

| 引脚 | Mode | Pull | Output Level | User Label |
|---|---|---|---|---|
| PA15 | Output Push Pull | No pull-up/down | **Low** | `GREEN` |
| PC12 | Output Push Pull | No pull | **Low** | `YELLOW` |
| PD2 | Output Push Pull | No pull | **Low** | `RED` |

**DHT11 数据引脚：**

| 引脚 | Mode | User Label |
|---|---|---|
| PD10 | Output Push Pull（代码里会动态切换） | `DHT11` |

> 打上 `DHT11` 标签后，CubeMX 会在 main.h 生成 `DHT11_Pin` 和 `DHT11_GPIO_Port` 宏，DHT11.c 里用到了它们。硬件上 PD10 需外接 **4.7kΩ 上拉电阻**。

**按键引脚（EXTI 外部中断）：**

| 引脚 | Mode | Pull | User Label |
|---|---|---|---|
| PE13 | **GPIO_EXTI13** | Pull-up | `KEY3` |
| PE14 | **GPIO_EXTI14** | Pull-up | `KEY2` |
| PE15 | **GPIO_EXTI15** | Pull-up | `KEY1` |

在 `GPIO` 配置页把这三个的 `GPIO mode` 设为 **`External Interrupt Mode with Falling edge trigger detection`**（下降沿触发，因为按键按下接地）。

然后在 `System Core` → `NVIC` 中：
- ✅ 勾选 **`EXTI line[15:10] interrupts`**

**星闪模块控制引脚：**

| 引脚 | Mode | User Label |
|---|---|---|
| PE4 | Input | `XS_BUSY` |
| PE5 | Input | `XS_DOUT` |
| PE6 | Input | `XS_POWER_IND` |
| PC13 | Output Push Pull | `XS_RESET` |
| PF8 | Output Push Pull | `XS_POWER_CL` |
| PD7 | Output Push Pull | `I2C_ADDR` |

### 步骤 9：NVIC 优先级设置

`System Core` → `NVIC`，`Priority Group` 选 **`4 bits for pre-emption priority`**，各中断优先级保持默认 0 即可（本项目对优先级不敏感）。

### 步骤 10：工程输出设置

`Project Manager` 标签页：

**Project 子页：**
- `Project Name`: `factory-test`
- `Toolchain / IDE`: **`MDK-ARM`**，Version `V5`

**Code Generator 子页：**
- ✅ **`Generate peripheral initialization as a pair of '.c/.h' files per peripheral`** ← **必须勾选！**这样才会生成独立的 `i2c.c`/`usart.c`/`tim.c`/`gpio.c`
- ✅ `Keep User Code when re-generating`
- ✅ `Delete previously generated files when not re-generated`

点击右上角 **`GENERATE CODE`**。

---

## 阶段二：Keil 工程目录准备

### 步骤 11：创建 BSP 目录结构

在工程根目录下手动创建：

```
factory-test/
├── BSP/
│   ├── Inc/     ← 放所有 .h
│   └── Src/     ← 放所有 .c
```

### 步骤 12：在 Keil 中添加分组与头文件路径

1. 打开 `MDK-ARM/factory-test.uvprojx`
2. 点击工具栏 **`Manage Project Items`**（三个方块图标）
3. 在 `Groups` 中新建一个组，命名为 **`BSP`**
4. 选中 `BSP` 组，点 `Add Files`，把 `BSP/Src/` 下所有 `.c` 加进来（先加已有的，后面新建了再补）
5. 点击 **`Options for Target`**（魔术棒）→ `C/C++` 标签
6. 在 `Include Paths` 中添加：`..\BSP\Inc`
7. 在 `Define` 中确认有 `USE_HAL_DRIVER, STM32H7xxxx`

---

## 阶段三：按顺序创建源文件（增量式）

> **核心原则：每写完一个文件就编译一次**，保证随时可回退。

### 步骤 13：`BSP/Inc/list.h` —— 链表（第一个，无依赖）

新建文件，内容为标准的 Linux 双向链表实现，至少需要这些：

```c
#ifndef __LIST_H
#define __LIST_H

#include <stddef.h>

struct list_head { struct list_head *next, *prev; };

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define INIT_LIST_HEAD(ptr) do { (ptr)->next=(ptr); (ptr)->prev=(ptr); } while(0)

static inline void __list_add(struct list_head *new,
                              struct list_head *prev, struct list_head *next)
{ next->prev=new; new->next=next; new->prev=prev; prev->next=new; }

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{ __list_add(new, head->prev, head); }

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each_entry_safe(pos, n, head, member)             \
    for (pos = list_entry((head)->next, typeof(*pos), member),     \
         n   = list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head);                                   \
         pos = n, n = list_entry(n->member.next, typeof(*n), member))

#endif
```

**编译验证**：此时应该 0 错误。

### 步骤 14：`queue.h` + `queue.c` —— 环形队列

**增量步骤：**
1. 先写 `queue.h`（`BUFFER_SIZE 128` + 两个函数声明）
2. 再写 `queue.c`：
   - 先定义 `CircularBuffer` 结构体和 `static cirbuffer`
   - 写 `initBuffer()`（两行）
   - 写 `queue_in()`：**先写判满**，再写存数据+移指针
   - 写 `queue_out()`：**先写判空**，再写取数据+移指针
3. 关键点：判满用 `(head+1)%SIZE == tail`，牺牲一格区分空满

**编译验证**：0 错误。

### 步骤 15：`usr_uart.h` + `usr_uart.c` —— printf 重定向

**增量步骤：**
1. `usr_uart.h`：声明 `UART_SenddBuf` 和 `extern uint8_t dataRcvd`
2. `usr_uart.c`：
   - 定义 `uint8_t dataRcvd;`
   - `extern UART_HandleTypeDef huart1;`
   - 实现 `UART_SenddBuf()` 包装 `HAL_UART_Transmit`
   - 实现 `int fputc(int ch, FILE *f)` 调用它

**⚠️ Keil 必做设置**：`Options for Target` → `Target` 标签 → ✅ 勾选 **`Use MicroLIB`**。不勾选的话 `fputc` 重定向不生效，printf 会卡死。

**第一次实机验证**：
```c
// 在 main.c 的 USER CODE BEGIN 2 里临时加：
printf("hello world\r\n");
```
烧录，打开串口助手（115200），看到 `hello world` 说明 printf 通了 —— **这是最重要的里程碑，后面所有调试都靠它**。

### 步骤 16：`split_argv.c` —— 分词器

**增量步骤：**
1. 定义 `split_state_t` 枚举（5 个状态）
2. 定义 `END_ARG()` 宏
3. 写 `console_split_argv()` 函数骨架：双指针 `in_ptr` / `out_ptr` 循环
4. 逐个填 `switch(state)` 的 5 个 case
5. 最后写收尾三步：`*out_ptr=0`、补最后一个参数、`argv[argc]=NULL`

**验证方法**：临时在 main 里测：
```c
char line[] = "set -i 192.168.1.1";
char *argv[6];
int argc = console_split_argv(line, argv, 6);
printf("argc=%d, [0]=%s [1]=%s [2]=%s\r\n", argc, argv[0], argv[1], argv[2]);
```

### 步骤 17：`console.h` —— 控制台头文件

**增量步骤：**
1. 先写包含（queue.h / usr_uart.h / list.h）
2. 写 `PRINTF` / `SPRINTF` 宏
3. 写全部 ANSI 转义宏（颜色、清屏、光标）
4. 写 `ERR` / `INFO` 宏（用 `do{}while(0)` 包裹）
5. 定义 `console_cmd_func_t` 函数指针类型
6. 定义 `cmd_item_t` 结构体（**注意必须包含 `struct list_head list;`**）
7. 声明三个对外函数

### 步骤 18：`console.c` —— 控制台核心（分 4 小步）

**第 18.1 步：骨架 + 注册函数**
```c
static struct list_head cmd_list;
static HANDLE_TYPE_S Handle = {0};

void console_cmd_register(cmd_item_t *cmd) { /* malloc + strcpy + list_add_tail */ }
void console_init(void) { INIT_LIST_HEAD(&cmd_list); /* 先只做这一句 */ }
void console_run(void) { }   // 空实现
```
编译通过。

**第 18.2 步：加 4 个系统命令**
- 实现 `console_clear` / `console_help`（先只写这两个简单的）
- 实现 `system_cmd_register()` 注册它们
- 在 `console_init()` 里调用 `system_cmd_register()` + 打欢迎屏

**第 18.3 步：实现 `console_run()`**
- 先只写 `while(queue_out(&data))` + `default: Handle.buff[Handle.len++]=data`
- 再加 `case KEY_ENTER: goto deal`
- 再写 `deal:` 段的分词 + 链表遍历 + 调用
- 最后加 `case KEY_BACKSPACE`

**第 18.4 步：补 `show` 和 `auto`**
- 加 `success`/`fail` 计数逻辑
- 加 `strlen(pos->command) > 3` 的过滤（**否则 auto 会递归死循环**）

**实机验证**：此时在 main.c 里加：
```c
console_init();
HAL_UART_Receive_IT(&huart1, &dataRcvd, 1);
while(1) { console_run(); }
```
并在 `HAL_UART_RxCpltCallback` 里加 `queue_in((char*)&dataRcvd); HAL_UART_Receive_IT(&huart1,&dataRcvd,1);`

串口敲 `help` 回车，能看到命令列表 → **控制台框架完成**。

### 步骤 19：`BH1750.h` + `BH1750.c` —— 第一个传感器（最简单）

**增量步骤（5 小步）：**

1. **写头文件**：4 个函数声明 + `extern uint16_t lx_Data`

2. **写宏定义区**：
   - 4 个地址宏（`0x46`/`0x47` 是 ADDR 接地时的 8 位地址）
   - 11 个命令宏（`0x00` POWER_DOWN ~ `0x60` CNG_TIME_LOW）

3. **写设备结构体 + 底层发送**：
   ```c
   typedef struct BH1750_device { ... } BH1750_device_t;
   static HAL_StatusTypeDef BH1750_send_command(dev, cmd)
       → HAL_I2C_Master_Transmit(dev->i2c_handle, dev->address_w, &cmd, 1, 10)
   ```

4. **写初始化**：
   - `BH1750_init_dev_struct()`：按 `addr_grounded` 选地址 + 存句柄
   - `BH1750_init_dev()`：**严格按顺序发 3 条命令** POWER_ON → RESET → H_RES_MODE
   - `BH1750_INIT()`：串起来，绑定 `&hi2c3`，第4参数传 `true`

5. **写读取 + 命令**：
   - `BH1750_get_lumen()`：`HAL_I2C_Master_Receive(..., 2, 10)` → 拼 16 位 → **除以 1.2**
   - `bh7150_get_data()`：调用它并更新 `lx_Data`
   - `bh7150_read()`：控制台回调，printf 结果，return 1/0
   - `BH7150_cmd_register()`：注册命令 `"1"`

**实机验证**：main 里加 `BH1750_INIT();` 和命令注册，串口敲 `1`，能看到 lx 数值 → **第一个传感器通了**。

### 步骤 20：`SCD04.h` + `SCD04.c` —— CO₂ 传感器

**增量步骤（5 小步）：**

1. **头文件**：声明 `SCD04_INIT` / `SCD04_cmd_register` / `scd04_get_Data` / `scd04_collect` / `scd04_read_data` / `extern uint16_t data_co2`

2. **宏定义**：地址 `(0x62<<1)` 和 `(0x62<<1|1)`，以及全部 16 位命令码

3. **底层三件套**：
   - `sensor_send_command()` → `HAL_I2C_Master_Transmit`
   - `sensor_recv()` → `HAL_I2C_Master_Receive`
   - `scd04_send_command()` → **大端拆分 `data[0]=cmd>>8; data[1]=(uint8_t)cmd;`**

4. **先实现探测**（最容易验证）：
   - `scd94_get_serial()`：发 `0x3682` → 读 9 字节 → 跳过 `i%3==0` 的 CRC 打印
   - `scd04_test()` + `SCD04_cmd_register()` 注册 `"8"`
   - **验证**：敲 `8`，能打印出 6 个十六进制字节 → I2C 通了

5. **再实现采集**：
   - `scd04_get_Data()`：发 `0x219D` → `HAL_Delay(5000)` → 发 `0xEC05` → `HAL_Delay(1)` → 读 9 字节 → `data_co2 = buf[0]<<8|buf[1]`
   - 拆出 `scd04_collect()`（只发采集命令）和 `scd04_read_data()`（读命令+读数据）

### 步骤 21：`DHT11.c` —— 单总线（最难，分 6 小步）

1. **先写 `Delay_us()` 并验证**：
   ```c
   // 验证方法：用示波器/逻辑分析仪看 PD10
   DHT11_Init_Out();
   while(1) {
       DHT11_BitValue(GPIO_PIN_SET);   Delay_us(1000);
       DHT11_BitValue(GPIO_PIN_RESET); Delay_us(1000);
   }
   // 应该看到 500Hz 方波，高低各 1ms。不对就调 differ 里的 "-5" 补偿值
   ```

2. **写 GPIO 模式切换**：`DHT11_Init_Out()` / `DHT11_Init_In()`

3. **写起始时序 `DHT11_Start()`**：拉低 20ms → 拉高 60μs → 切输入

4. **写 `DHT11_ReadByte()`**：for 8 次 { 等低电平结束 → Delay_us(50) → 采样判 0/1 → 若是1则等高电平结束 }

5. **写 `DHT11_Data()`**：
   - 调 Start
   - 等应答（带 retry 超时）
   - 循环读 5 字节
   - 恢复输出+拉高
   - 校验和判断 + 温湿度换算

6. **写命令回调**：`DHT11_get()` 里**必须加 `__disable_irq()` / `__enable_irq()` 包裹**，后面加 `Delay_ms(2000)`

**验证**：敲 `4`，看到合理的温湿度（如 25.0 / 55.0）→ 成功。如果读出全 0，检查上拉电阻和 `Delay_us` 精度。

### 步骤 22：串口模块（bmp280 / csb / gnss / EB21 / lcd）

这 5 个是同一套模板，**按此顺序写，每个 4 小步：**

**通用模板：**
```c
// ① 头文件：定义 BUFFER_LEN、FRAME_LEN，extern 出 buffer/count/temp/frame_ready
// ② .c 里定义这些全局变量 + static gn_buffer 备份区
// ③ 写 xxx_deal_buffer()：备份→清空→解析→复位标志
// ④ 写 xxx_test() + xxx_cmd_register()
// ⑤ 【关键】去 main.c 的 HAL_UART_RxCpltCallback 里加对应的 if 分支
```

**建议顺序：**
1. **`EB21.c`** 最简单（无协议，全收）—— 先熟悉模板
2. **`lcd.c`** 定长 4 字节 —— 学"收满就处理"
3. **`csb.c`** 帧头 + 定长 —— 学"帧头同步"
4. **`gnss.c`** 帧头 + 帧尾 —— 学"变长帧"
5. **`bmp280.c`** 帧头 + 帧尾 + strtok 解析 —— 最复杂

**每写完一个就在 main.c 加中断分支 + `HAL_UART_Receive_IT` 启动 + 命令注册，然后实机验证。**

---

## 阶段四：main.c 主逻辑组织

### 步骤 23：按顺序填写 main.c 的各个 USER CODE 段

**23.1 — `USER CODE BEGIN Includes`：**
```c
#include <stdbool.h>
#include "csb.h"
#include "usr_uart.h"
#include "console.h"
#include "lcd.h"
#include "BH1750.h"
#include "bmp280.h"
#include "SCD04.h"
#include "DHT11.h"
#include "EB21.h"
#include "gnss.h"
```

**23.2 — `USER CODE BEGIN PFP`：** 全局标志 + 周期宏 + 软计数器 + `co2_status` 枚举

**23.3 — `USER CODE BEGIN 0`：** 按此顺序写 6 个函数：
1. `HAL_TIM_PeriodElapsedCallback()` —— 4 行减法
2. `HAL_UART_RxCpltCallback()` —— 6 个 if 分支
3. `HAL_GPIO_EXTI_Callback()` —— 3 个 case
4. `lcd_set_page0()` / `lcd_set_page1()`
5. `key_function()`
6. `timer_collect_data()` / `get_sensor_data()`

**23.4 — `USER CODE BEGIN 2`：** 严格按这个顺序（顺序不能乱）：
```c
HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);          // 1. PWM
__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
SCD04_INIT();                                       // 2. 传感器
BH1750_INIT();
HAL_UART_Receive_IT(&huart1, &dataRcvd, 1);        // 3. 6路串口中断
/* ...其余5路... */
HAL_TIM_Base_Start_IT(&htim2);                     // 4. 节拍
console_init();                                     // 5. 控制台（必须在注册命令前）
BH7150_cmd_register(); /* ...8个... */              // 6. 注册命令
get_sensor_data();                                  // 7. 首采
lcd_set_page0();                                    // 8. 显示
```

**23.5 — `while(1)`：**
```c
while (1)
{
    console_run();
    key_function();
    timer_collect_data();
}
```

### 步骤 24：最终整机验证清单

| 序号 | 验证项 | 预期现象 |
|---|---|---|
| 1 | 上电 | 串口打印欢迎屏 + `-> console:` |
| 2 | 敲 `help` | 列出 12 条命令 |
| 3 | 敲 `1` ~ `8` | 各自打印数据 + PASS/FAIL |
| 4 | 敲 `auto` | 连续跑完 8 项 |
| 5 | 敲 `show` | 打印统计表 |
| 6 | 等 8 秒后看 LCD | 显示温湿度/光照 |
| 7 | 按 KEY1/KEY2 | LCD 在两页间切换 |
| 8 | 按 KEY3 | 三灯全亮 + 蜂鸣器响；再按一次全灭 |
| 9 | 手挡超声波 | 距离<30cm 时三灯自动亮 |

---

## 阶段五：可调参数完整清单

### 5.1 采集周期类（`main.c`）

| 参数 | 当前值 | 单位 | 调大的影响 | 调小的影响 | 安全范围 |
|---|---|---|---|---|---|
| `BH1750_TIMER_PERIOD` | 3 | 秒 | 光照刷新慢，更省电 | 刷新快，I2C 占用增加 | ≥1（芯片 120ms 转换） |
| `DHT11_TIMER_PERIOD` | 10 | 秒 | 温湿度刷新慢 | **<1 会读取失败** | **必须 ≥2** |
| `SCD04_TIMER_PERIOD` | 5 | 秒 | CO₂ 数据偏旧 | **<5 会读到上次的旧值** | **必须 ≥5** |
| `CO2_READ_TIMER_PERIOD` | 1 | 秒 | CO₂ 整体周期变长 | 传感器更频繁工作，发热大 | ≥1 |
| `RFID_TIMER_PERIOD` | 2 | 秒 | 无影响（未使用） | 无影响 | 任意 |

### 5.2 定时器硬件参数（`Core/Src/tim.c`）

| 参数 | 当前值 | 影响 |
|---|---|---|
| `htim1.Init.Prescaler` | `64-1` | **改了 DHT11 就会失效**。必须保证 `时钟/(PSC+1) = 1MHz` |
| `htim2.Init.Prescaler` | `6399` | 改成 `3199` → 节拍变 0.5 秒，所有采集周期减半 |
| `htim2.Init.Period` | `9999` | 同上，两者共同决定节拍 |
| `htim3.Init.Period` | `100-1` | PWM 分辨率。改成 `1000-1` 则占空比可调 0~999，更细腻 |

### 5.3 阈值与判据类

| 参数 | 位置 | 当前值 | 说明 |
|---|---|---|---|
| **超声波报警距离** | `csb.c:28` | `300` | 单位 mm。改成 `500` = 50cm 内就亮灯 |
| **PWM 蜂鸣器音量** | `main.c:216` | `99` | 范围 0~99。改小音量降低（如 `50`） |
| **BH1750 转换系数** | `BH1750.c:103` | `1.2` | 校准用。测得偏高就调大（如 `1.25`） |
| **DHT11 采样延时** | `DHT11.c:65` | `Delay_us(50)` | 判 0/1 的采样点。读数不稳可微调 40~55 |
| **DHT11 起始拉低** | `DHT11.c:81` | `20000` (20ms) | 手册要求 ≥18ms。可调 18000~25000 |
| **DHT11 冷却时间** | `DHT11.c:135` | `2000` ms | 不能小于 1000 |
| **LCD 流控延时** | `lcd.c:84,97` | `500` ms | 屏响应慢就调大；屏快可调到 `200` 提升流畅度 |
| **LCD 测试等待** | `lcd.c:49` | `2000` ms | 等 `OK` 回应的时间 |
| **星闪等待回应** | `EB21.c:22` | `1000` ms | 模块回应慢就调大 |
| **SCD41 转换等待** | `SCD04.c:186` | `5000` ms | **不能小于 5000**（手册硬性要求） |
| **开机预热** | `main.c:367` | `3000` ms | 缩短可加快开机，但传感器可能不稳 |

### 5.4 缓冲区尺寸类

| 参数 | 位置 | 当前值 | 调整建议 |
|---|---|---|---|
| `BUFFER_SIZE` | `queue.h:9` | 128 | 串口高速输入丢字符时调大到 256/512 |
| `MAX_ARGS` | `console.c:5` | 6 | 需要更多命令参数时调大 |
| `COMMAND_LEN` | `console.h:66` | 24 | 命令名超长时调大 |
| `HELP_LEN` | `console.h:67` | 200 | **每个命令占 200 字节堆空间**，命令多时可减小到 64 省 RAM |
| `GNSS_BUFFER_LEN` | `gnss.h:8` | 1024 | NMEA 单句 <82 字节，可减到 128 省 RAM |
| `CSB_BUFFER_LEN` | `csb.h:8` | 128 | 帧只有 4 字节，可减到 16 |
| `LCD_BUFFER_LEN` | `lcd.h:8` | 20 | 回应只有 4 字节，够用 |

### 5.5 通信参数类（需在 CubeMX 同步修改）

| 参数 | 位置 | 当前值 | 说明 |
|---|---|---|---|
| UART5 波特率 | `usart.c:89` | **9600** | 超声波模块固定，改了通信失败 |
| 其他串口波特率 | `usart.c` | 115200 | 可统一改，但要同步改串口助手 |
| I2C Timing | `i2c.c:42,83` | `0x00707CBB` | 对应 100kHz。改 400kHz 需 CubeMX 重算 |
| BH1750 地址 | `BH1750.c:147` | `true`(接地) | ADDR 引脚接高电平时改 `false` |
| BH1750 分辨率模式 | `BH1750.c:84` | `CMD_H_RES_MODE` | 改 `CMD_H_RES_MODE2` 得 0.5lx 分辨率（需除以 2） |

---

## 阶段六：常见问题排查表

| 现象 | 可能原因 | 排查方法 |
|---|---|---|
| printf 无输出 | 没勾 MicroLIB | Options→Target→Use MicroLIB |
| printf 卡死 | USART1 未初始化就调用 | 确认 `MX_USART1_UART_Init()` 在前 |
| 命令敲了没反应 | 忘了 `HAL_UART_Receive_IT` 续期 | 检查回调末尾 |
| 只能收到 1 个字符 | 同上 | 同上 |
| I2C 读全 0xFF | 没接上拉电阻 / 地址错 | 万用表量 SCL/SDA 静态是否 3.3V |
| DHT11 读数全 0 | 时序不准 / 没关中断 | 示波器验证 `Delay_us` |
| DHT11 偶尔失败 | 采样太频繁 | 确认间隔 ≥2 秒 |
| CO₂ 一直是旧值 | 5 秒等待不够 | 确认 `SCD04_TIMER_PERIOD ≥ 5` |
| LCD 不刷新 | `lcd_ready` 永远 false | 检查屏是否回 `OK\r\n`，用串口助手直连屏测试 |
| 按键无反应 | EXTI 没使能 | CubeMX 确认勾了 `EXTI line[15:10]` |
| 按键抖动多次触发 | 消抖不足 | 在回调里加 `HAL_GetTick()` 时间戳判断 |
| 开 -O2 优化后功能异常 | 缺 `volatile` | 给跨中断变量加 `volatile` |
| 定时采集突然停止 | 忘了 `HAL_TIM_Base_Start_IT` | 检查 `timer_collect_data` 的 `end:` 标签 |

---

## 附录：项目已知缺陷清单

如果你要复刻并**改进**，以下是原项目中发现的问题：

| 编号 | 文件位置 | 问题 | 建议修复 |
|---|---|---|---|
| 1 | `bmp280.c:66` | `receive_success` 从不置 true，永远 FAIL | 在 `bmp280_deal_buffer()` 里加 `receive_success = true;` |
| 2 | `csb.c:46` | 字节序与 `deal_buffer` 不一致 | 改为 `gn_buffer[1]<<8 \| gn_buffer[2]` |
| 3 | `BH1750.c:123` | `uint16_t == -1` 永远为假 | 改为 `== 0xFFFF` |
| 4 | `main.c:136` | GNSS 帧头判断逻辑反了 | 改为 `if (gnss_check_frame())` |
| 5 | `DHT11.c:118` | `arr[3] \| 0x7F` 应为 `&` | 改为 `&`，或直接删除（DHT11 无负温） |
| 6 | `DHT11.c:122` | `arr[3]/10` 整数除法丢精度 | 改为 `arr[3]/10.0f` |
| 7 | `usr_uart.c:18` | `fputc` 无 return | 加 `return ch;` |
| 8 | `SCD04.c:217` | `scd04_collect` 成功路径无 return | 加 `return 1;` |
| 9 | `console.c:175` | `malloc` 未判 NULL、未初始化计数器 | 加判空 + `node->success = 0; node->fail = 0;` |
| 10 | `main.c` 全局标志 | 缺 `volatile` | 给 `key_x_touch`/`lcd_ready`/传感器数据加 `volatile` |
| 11 | `main.c:174` | `xs_buffer` 无边界检查 | 加 `if (xs_count < XS_BUFFER_LEN)` |
| 12 | `DHT11.c:100` | `while(高电平)` 无超时，可能死锁 | 加 retry 计数 |
| 13 | `lcd.c:37` | `lcd_test` 声明 void 却 return int | 改为 `static int lcd_test(int argc, char **argv)` |
| 14 | `lcd.h:14` | `extern lcd_frame_ready` 与实际 `lcd_ready` 不符 | 改为 `extern bool lcd_ready;` |
| 15 | `csb.c:6` | `gn_buffer` 非 static，污染全局命名空间 | 加 `static` |

---

## 总结：这个项目值得学习的 8 个设计亮点

1. **链表式命令注册** —— 新增传感器只需写一个 `xxx_cmd_register()`，零侵入，`console.c` 一行不用改
2. **软件定时器分频** —— 一个硬件定时器 + N 个整数计数器 = N 个不同周期的任务
3. **CO₂ 两阶段状态机** —— 把 5 秒阻塞拆成"发命令 → 等待 → 读结果"，CPU 利用率从接近 0 提升到接近 100%
4. **中断置标志 + 主循环执行** —— KEY1/KEY2 的处理方式是嵌入式中断设计的教科书示范
5. **环形队列解耦** —— 串口中断和命令解析完全异步，互不阻塞
6. **`strlen(command) > 3` 过滤技巧** —— 用命令名长度区分系统命令和测试命令，省掉一个标志位字段
7. **不用 PLL 只用 HSI** —— 产线测试固件的正确取舍：宁可慢，也要保证晶振有问题时板子还能启动报错
8. **`fs_status` 手动/自动互斥** —— 用一个 bool 优雅解决了"按键控灯"和"超声波自动控灯"的冲突

---
