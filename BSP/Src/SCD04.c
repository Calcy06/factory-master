#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "main.h"
#include "console.h"
#include "SCD04.h"

#define READ_CMD "-r"
#define SERIAL_CMD "-s"
#define STATUS_CMD "-st"
#define SINGLE_CMD "-sg"

#define SCD4x_ADDR_WRITE (0x62 << 1)
#define SCD4x_ADDR_READ (0x62 << 1 | 0x1)

// Basic Commands
#define SCD4x_COMMAND_START_PERIODIC_MEASUREMENT 0x21b1
#define SCD4x_COMMAND_READ_MEASUREMENT 0xec05          // execution time: 1ms
#define SCD4x_COMMAND_STOP_PERIODIC_MEASUREMENT 0x3f86 // execution time: 500ms

// On-chip output signal compensation
#define SCD4x_COMMAND_SET_TEMPERATURE_OFFSET 0x241d // execution time: 1ms
#define SCD4x_COMMAND_GET_TEMPERATURE_OFFSET 0x2318 // execution time: 1ms
#define SCD4x_COMMAND_SET_SENSOR_ALTITUDE 0x2427    // execution time: 1ms
#define SCD4x_COMMAND_GET_SENSOR_ALTITUDE 0x2322    // execution time: 1ms
#define SCD4x_COMMAND_SET_AMBIENT_PRESSURE 0xe000   // execution time: 1ms

// Field calibration
#define SCD4x_COMMAND_PERFORM_FORCED_CALIBRATION 0x362f             // execution time: 400ms
#define SCD4x_COMMAND_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED 0x2416 // execution time: 1ms
#define SCD4x_COMMAND_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED 0x2313 // execution time: 1ms

// Low power
#define SCD4x_COMMAND_START_LOW_POWER_PERIODIC_MEASUREMENT 0x21ac
#define SCD4x_COMMAND_GET_DATA_READY_STATUS 0xe4b8 // execution time: 1ms

// Advanced features
#define SCD4x_COMMAND_PERSIST_SETTINGS 0x3615      // execution time: 800ms
#define SCD4x_COMMAND_GET_SERIAL_NUMBER 0x3682     // execution time: 1ms
#define SCD4x_COMMAND_PERFORM_SELF_TEST 0x3639     // execution time: 10000ms
#define SCD4x_COMMAND_PERFORM_FACTORY_RESET 0x3632 // execution time: 1200ms
#define SCD4x_COMMAND_REINIT 0x3646                // execution time: 20ms

// Low power single shot - SCD41 only
#define SCD4x_COMMAND_MEASURE_SINGLE_SHOT 0x219d          // execution time: 5000ms
#define SCD4x_COMMAND_MEASURE_SINGLE_SHOT_RHT_ONLY 0x2196 // execution time: 50ms

uint16_t data_co2 = -1;

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

static void SCD04_init_dev_struct(device_t *dev, I2C_HandleTypeDef *i2c_handle,
                                  char *name, uint8_t addr_grounded)
{

    dev->address_r = SCD4x_ADDR_READ;
    dev->address_w = SCD4x_ADDR_WRITE;
    dev->i2c_handle = i2c_handle;

    strcpy(dev->name, name);
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

static HAL_StatusTypeDef scd94_get_serial(device_t *dev)
{

    scd04_send_command(dev, SCD4x_COMMAND_GET_SERIAL_NUMBER, 2);

    if (sensor_recv(dev, 9) != HAL_OK)
    {
        return HAL_ERROR;
    }

    PRINTF("\r\n serial namber:");
    /* 按照数据手册计算 */
    for (char i = 1; i <= 9; i++)
    {
        if (i % 3 == 0)
        {
            continue;
        }
        PRINTF("%02x ", dev->buffer[i - 1]);
    }
    PRINTF("\r\n");
    memset(dev->buffer, 0, 9);
    return HAL_OK;
}

static int scd04_test(int argc, char **argv)
{
    PRINTF("\r\n---------二氧化碳传感器测试--------------- \r\n");

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
    SCD04_init_dev_struct(&scd04_dev, &hi2c2, "SCD04", true);
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
