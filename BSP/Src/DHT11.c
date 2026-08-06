#include "main.h"
#include "stdint.h"
#include "console.h"

#define READ_CMD "-r"
#define DHT11_GPIO_DATA GPIO_PIN_10
#define DHT11_GPIO_BASE GPIOD

#define DHT11_GET_DATA HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA)

extern TIM_HandleTypeDef htim1;

#define Delay_ms(x) HAL_Delay(x)

static void Delay_us(uint16_t us)
{ // 微秒延时
    uint16_t differ = 0xffff - us - 5;
    __HAL_TIM_SET_COUNTER(&htim1, differ); // 设定TIM1计数器起始值
    HAL_TIM_Base_Start(&htim1);            // 启动定时器
    while (differ < 0xffff - 5)
    {                                           // 判断
        differ = __HAL_TIM_GET_COUNTER(&htim1); // 查询计数器的计数值
    }
    HAL_TIM_Base_Stop(&htim1);
}

static void DHT11_BitValue(GPIO_PinState action)
{
    HAL_GPIO_WritePin(DHT11_GPIO_BASE, DHT11_GPIO_DATA, action);
}

static void DHT11_Init_Out(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; // 推挽输出
    GPIO_InitStruct.Pin = DHT11_GPIO_DATA;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT11_GPIO_BASE, &GPIO_InitStruct);
}

static void DHT11_Init_In(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pin = DHT11_GPIO_DATA;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_GPIO_BASE, &GPIO_InitStruct);
}

static uint8_t DHT11_ReadByte(void)
{
    uint8_t data = 0x00; // 给数据付一个初始值
    uint8_t i = 0;
    for (i = 0; i < 8; i++) // 循环8次，接受一个字节
    {
        while (HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA) == GPIO_PIN_RESET)
            ; // 等待低电平结束，进入下一位

        Delay_us(50);

        if (HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA) == GPIO_PIN_SET) // 此时检测电平高低，若为高电平
        {
            data |= (0x80 >> i); // 将该位置一
            while (HAL_GPIO_ReadPin(DHT11_GPIO_BASE, DHT11_GPIO_DATA) == GPIO_PIN_SET)
                ; // 等待高电平结束，进入下一位
        }
    }
    return data; // 返回一个字节的数据
}

static void DHT11_Start(void)
{
    DHT11_Init_Out();               // 主机输出模式
    DHT11_BitValue(GPIO_PIN_RESET); // 主机拉低总线
    Delay_us(20000);                // 延时20ms
    DHT11_BitValue(GPIO_PIN_SET);   // 主机再次拉高总线
    Delay_us(60);                   // 延时20us
    DHT11_Init_In();                // 开始时序结束，总线交于DHT11控制
}

void DHT11_Data(float *temp, float *hum)
{
    int8_t i = 0;
    int8_t arr[5];

    DHT11_Start(); // 通信开始时序
                   // DHT11_BitValue(GPIO_PIN_SET); // 拉高电平，方便检测DHT进入低电平

    while (DHT11_GET_DATA == GPIO_PIN_RESET)
        ; // 低电平等待跳变高电平

    while (DHT11_GET_DATA == GPIO_PIN_SET)
        ; // 高电平等待跳变低电平、

    for (i = 0; i < 5; i++)
    {
        arr[i] = DHT11_ReadByte();
    }
    // DHT11_BitValue(GPIO_PIN_RESET); // 通信结束，DHT11拉低总线
    Delay_us(50);
    DHT11_Init_Out();                                // 总线交给主机
    DHT11_BitValue(GPIO_PIN_SET);                    // 拉高总线进入空闲
    if (arr[0] + arr[1] + arr[2] + arr[3] == arr[4]) // 校验位检测
    {
        *temp = arr[2];
        *hum = arr[0];
    }
    if (arr[3] & 0x80)
    {
        *temp = (-1) * ((arr[3] | 0x7F) / 10 + arr[2]);
    }
    else
    {
        *temp = (arr[3] / 10 + arr[2]);
    }

    *hum = arr[0] + arr[1] / 10;
}

static int DHT11_get(int argc, char **argv)
{
    float tem = 0, hum = 0;
    PRINTF("\r\n---------温湿度传感器测试--------------- \r\n");
    __disable_irq();
    DHT11_Data(&tem, &hum);
    __enable_irq();
    Delay_ms(2000);
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
