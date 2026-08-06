// –––––––––––––––––1.开始（模块准备 + 命令入口）––––––––––––––––––––
#include "main.h"
#include "stdio.h"
#include "string.h"
#include "console.h"

#define READ_CMD "-r" //"-r" 读命令
#define DHT11_Pin GPIO_PIN_10
#define DHT11_GPIO_Port GPIOD

extern TIM_HandleTypeDef htim1; // TIM1句柄（微秒时钟）
float tem, hum = 0;             // 全局温湿度缓存

static uint8_t DHT11_ReadByte(void);
void DHT11_Data(float *tem, float *hum);

// 微秒延时：借助 TIM1 定时器当"微秒表"（TIM1 须配成 1MHz 计数）
static void Delay_us(uint16_t us)
{
    uint16_t differ = 0xffff - us - 5;     // 计算计数器起点
    __HAL_TIM_SET_COUNTER(&htim1, differ); // 设定TIM1计数器起始值
    HAL_TIM_Base_Start(&htim1);            // 启动定时器(向上计数)
    while (differ < 0xffff - 5)            // while(){读取当前的值}
    {
        differ = __HAL_TIM_GET_COUNTER(&htim1); //    实时读取当前计数值
    }
    HAL_TIM_Base_Stop(&htim1); // 关闭计数器
}

//------------------------2.初始化引脚------------------------------

// 初始化为输出模式（主机发信号时用）
static void DHT11_Init_Out()
{
    __HAL_RCC_GPIOD_CLK_ENABLE();                     // ① 开启 GPIOD 时钟
    GPIO_InitTypeDef GPIO_Initstruct = {0};           // 定义GPIO结构体并成员全部清零初始化
    GPIO_Initstruct.Pin = DHT11_Pin;                  //    引脚 = DHT11_Pin
    GPIO_Initstruct.Mode = GPIO_MODE_OUTPUT_PP;       // ② 推挽输出
    GPIO_Initstruct.Pull = GPIO_NOPULL;               //    无上下拉
    HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_Initstruct); // ③ 应用配置
}

// 初始化为输入模式（收 DHT11 数据时用）
static void DHT11_Init_In()
{
    __HAL_RCC_GPIOD_CLK_ENABLE();                     // ① 开启 GPIOD 时钟
    GPIO_InitTypeDef GPIO_Initstruct = {0};           // 定义GPIO结构体并成员全部清零初始化
    GPIO_Initstruct.Pin = DHT11_Pin;                  //    引脚 = DHT11_Pin
    GPIO_Initstruct.Mode = GPIO_MODE_INPUT;           // ② 浮空输入
    GPIO_Initstruct.Speed = GPIO_SPEED_FREQ_HIGH;     // ③ 高速
    HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_Initstruct); // ④ 应用配置
}

//---------------------3.发起起始信号--------------------
static void DHT11_Start()
{
    DHT11_Init_Out();                                              // ① 切到输出模式，主机掌握总线
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET); // ② 主机把总线拉低
    Delay_us(20000);                                               // ③ 拉低 20ms（>18ms，满足唤醒要求）
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);   // ④ 主机释放总线（拉高）
    Delay_us(60);                                                  // ⑤ 拉高约 60us，给 DHT11 反应时间
    DHT11_Init_In();                                               // ⑥ 切到输入模式，总线交给 DHT11 控制（进入应答阶段）
}

//----------------------------4.检测 DHT11 响应---------------------------
void DHT11_Data(float *tem, float *hum)
{
    uint8_t i = 0;
    uint8_t arr[5] = {0}; // 存放 5 个字节：湿整、湿小、温整、温小、校验
    uint32_t retry = 0;

    DHT11_Start();                                                                          // 先发起始信号
    while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_RESET && retry < 10000) //  ① 等 DHT11 拉低（应答低电平）
    {
        retry++;
    }
    retry = 0;
    while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_SET && retry < 10000) // ② 等应答高电平结束
    {
        retry++;
    } //    至此 DHT11 应答完成，即将开始发 40 位数据

    for (i = 0; i < 5; i++) // 连续读 5 个字节
    {
        arr[i] = DHT11_ReadByte();
    }

    Delay_us(50);                                                // 收完最后一字节后的小延时
    DHT11_Init_Out();                                            // 总线交回主机
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET); // 拉高进入空闲

    if (arr[0] + arr[1] + arr[2] + arr[3] == arr[4]) // 校验：前4字节之和 == 第5字节
    {
        /* DHT11：温湿度为整数，小数恒为 0；&0x7F 仅对 DHT22 负温度有意义 */
        if (arr[3] & 0x80) // 负温度（DHT22 协议）
            *tem = -(arr[2] + (arr[3] & 0x7F) / 10.0f);
        else
            *tem = arr[2] + (arr[3] & 0x7F) / 10.0f;

        *hum = arr[0] + arr[1] / 10.0f;
    }
    /* 校验不通过则保持原值（丢弃本次错误数据） */
}

//-----------------------5. 逐字读取 5 个数据字节-------------------------
static uint8_t DHT11_ReadByte(void)
{
    uint8_t i = 0;
    uint8_t data = 0;       // 本字节结果初值 0
    for (i = 0; i < 8; i++) // 循环 8 次，逐位接收
    {
        uint32_t retry = 0;
        while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_RESET && retry < 10000) // ① 等"位前 50us 低电平"结束
        {
            retry++;
        }
        Delay_us(50); // ② 低电平结束后，延时 50us 再采样（此刻处于高电平窗口）

        if (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_SET) // ③ 采样点仍为高 → 这一位是 1
        {
            data |= (0x80 >> i); //    把第 i 位置 1（高位先传，第0轮填 bit7）
            retry = 0;
            while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_SET && retry < 10000) // ④ 等高电平结束，准备收下一位
            {
                retry++;
            }
        }
        // 若采样点为低，则这一位是 0，data 对应位保持 0，无需处理
    }
    return data; // 返回收到的 1 字节
}

static int DHT11_get(int argc, char **argv)
{
    tem = 0, hum = 0;
    PRINTF("\r\n---------温湿度传感器测试--------------- \r\n");
    __disable_irq();
    DHT11_Data(&tem, &hum);
    __enable_irq();
    Delay_us(2000);
    PRINTF("\r\n* TEM: %.1f HUM: %.1f \r\n", tem, hum);
    if (tem)
    {
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
