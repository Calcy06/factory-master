#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "main.h"
#include "console.h"
#include "SCD04.h"

// 串口控制台交互命令
#define READ_CMD "-r"    // 读取CO2、温湿度测量结果
#define SERIAL_CMD "-s"  // 读取传感器芯片序列号
#define STATUS_CMD "-st" // 获取传感器工作状态
#define SINGLE_CMD "-sg" // 触发传感器单次测量

// SCD4x I2C从机地址，适配HAL库（7位地址0x62左移1位）
#define SCD4x_ADDR_WRITE (0x62 << 1)      // I2C写地址 0xC4
#define SCD4x_ADDR_READ ((0x62 << 1) | 1) // I2C读地址 0xC5

// SCD4x I2C 16位命令码
#define SCD4x_COMMAND_READ_MEASUREMENT 0xec05    // 读取已完成的测量值，执行时间1ms
#define SCD4x_COMMAND_GET_SERIAL_NUMBER 0x3682   // 读取芯片唯一序列号，执行时间1ms
#define SCD4x_COMMAND_MEASURE_SINGLE_SHOT 0x219d // 单次完整测量CO2+温湿度，需等待5000ms

uint16_t data_co2 = 0;

typedef struct _device
{
    char name[10];

    I2C_HandleTypeDef *i2c_handle;
    uint8_t address_r;
    uint8_t address_w;

    uint8_t buffer[20];

} device_t;

extern I2C_HandleTypeDef hi2c2;
static device_t scd04_dev;

static void SCD04_init_dev_struct(device_t *dev, I2C_HandleTypeDef *i2c_handle,
                                  char *name, uint8_t addr_grounded)
{

    dev->address_r = SCD4x_ADDR_READ;
    dev->address_w = SCD4x_ADDR_WRITE;
    dev->i2c_handle = i2c_handle;

    strcpy(dev->name, name);
}

static HAL_StatusTypeDef sensor_send_command(device_t *dev, uint8_t *cmd, uint16_t size)
{
    // TODO hal checks
    if (HAL_I2C_Master_Transmit(
            dev->i2c_handle, // I2C Handle
            dev->address_w,  // I2C addr of dev
            cmd,             // CMD to be executed
            size,            // 8bit addr
            10               // Wait time
            ) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}

static HAL_StatusTypeDef scd04_send_command(device_t *dev, uint16_t cmd, uint16_t size)
{
    uint8_t data[10] = {0};
    data[0] = cmd >> 8;
    data[1] = (uint8_t)cmd;
    if (sensor_send_command(dev, data, size))
    {
        printf("\r\n CMD  0x%02x error.\r\n", cmd);
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef sensor_recv(device_t *dev, uint16_t size)
{
    if (HAL_I2C_Master_Receive(dev->i2c_handle,
                               dev->address_r,
                               dev->buffer,
                               size,
                               10) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}

// 单次采集 == 采集 + 读数据
int scd04_get_Data(void)
{

    if (scd04_send_command(&scd04_dev, SCD4x_COMMAND_MEASURE_SINGLE_SHOT, 2))
    {
        printf("\r\nHAL_ERROR SCD4x_COMMAND_MEASURE_SINGLE_SHOT \r\n");

        return HAL_ERROR;
    }
    HAL_Delay(5000);

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

// 采集
int scd04_collect(void)
{

    if (scd04_send_command(&scd04_dev, SCD4x_COMMAND_MEASURE_SINGLE_SHOT, 2))
    {
        printf("\r\nHAL_ERROR SCD4x_COMMAND_MEASURE_SINGLE_SHOT \r\n");

        return HAL_ERROR;
    }
}

// 读数据
int scd04_read_data(void)
{
    if (scd04_send_command(&scd04_dev, SCD4x_COMMAND_READ_MEASUREMENT, 2))
    {
        printf("\r\nHAL_ERROR SCD4x_COMMAND_READ_MEASUREMENT \r\n");

        return 0;
    }

    HAL_Delay(1);
    if (sensor_recv(&scd04_dev, 9) != HAL_OK)
    {
        printf("\r\n Recv data error.\r\n");
        return 0;
    }

    data_co2 = (uint16_t)scd04_dev.buffer[0] << 8 | (uint16_t)scd04_dev.buffer[1];

    memset(scd04_dev.buffer, 0, 9);
    return 1;
}

static int scd04_test(int argc, char **argv)
{
    PRINTF("\r\n---------二氧化碳传感器测试--------------- \r\n");

    if (scd04_read_data)
    {
        printf("\r\n* CO2_Data: %ldPPM\r\n", data_co2);
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
    SCD04_init_dev_struct(&scd04_dev, &hi2c2, "SCD04", true);
}
