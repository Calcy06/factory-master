#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "main.h"
#include "console.h"
#include "BH1750.h"

#define READ_CMD "-r"

#define BH1750_NO_GROUND_ADDR_WRITE (0xB9 + 0)
#define BH1750_NO_GROUND_ADDR_READ (0xB9 + 1)
#define BH1750_GROUND_ADDR_WRITE (0x46 + 0)
#define BH1750_GROUND_ADDR_READ (0x46 + 1)

// #define CMD_POWER_DOWN 0x00
#define CMD_POWER_ON 0x01
#define CMD_RESET 0x03
#define CMD_H_RES_MODE 0x10 // BH1750连续高分辨率测量模式
// #define CMD_H_RES_MODE2 0x11
// #define CMD_L_RES_MODE 0x13
// #define CMD_ONE_H_RES_MODE 0x20
// #define CMD_ONE_H_RES_MODE2 0x21
// #define CMD_ONE_L_RES_MODE 0x23
// #define CMD_CNG_TIME_HIGH 0x30 // 3 LSB set time
// #define CMD_CNG_TIME_LOW 0x60  // 5 LSB set time

uint16_t lx_Data = 0;
extern I2C_HandleTypeDef hi2c3;

typedef struct BH1750_device
{
    char name[10];

    I2C_HandleTypeDef *i2c_handle;

    uint8_t address_r;
    uint8_t address_w;

    uint16_t value;
    uint8_t buffer[2];
} BH1750_device_t;

static BH1750_device_t bh1750_dev = {0};

static HAL_StatusTypeDef BH1750_send_command(BH1750_device_t *dev, uint8_t cmd)
{
    if (HAL_I2C_Master_Transmit(dev->i2c_handle, dev->address_w, &cmd, 1, 10) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return HAL_OK;
}

static void BH1750_init_dev_struct(BH1750_device_t *dev, I2C_HandleTypeDef *i2c_handle, char *name, uint8_t addr_grounded)
{
    strcpy(dev->name, name);

    dev->i2c_handle = i2c_handle;

    if (addr_grounded)
    {
        dev->address_r = BH1750_GROUND_ADDR_READ;
        dev->address_w = BH1750_GROUND_ADDR_WRITE;
    }
    else
    {
        dev->address_r = BH1750_NO_GROUND_ADDR_READ;
        dev->address_w = BH1750_NO_GROUND_ADDR_WRITE;
    }
}

static HAL_StatusTypeDef BH1750_init_dev(BH1750_device_t *dev)
{
    BH1750_send_command(dev, CMD_POWER_ON);
    BH1750_send_command(dev, CMD_RESET);
    BH1750_send_command(dev, CMD_H_RES_MODE);

    return HAL_OK;
}

void BH1750_INIT()
{
    BH1750_init_dev_struct(&bh1750_dev, &hi2c3, "BH1750", true);
    BH1750_init_dev(&bh1750_dev);
}

static HAL_StatusTypeDef BH1750_get_lumen(BH1750_device_t *dev)
{
    if (HAL_I2C_Master_Receive(dev->i2c_handle, dev->address_r, dev->buffer, 2, 10) != HAL_OK)
    {
        return HAL_ERROR;
    }

    dev->value = ((uint16_t)dev->buffer[0] << 8) | dev->buffer[1];
    dev->value /= 1.2;

    return HAL_OK;
}

uint16_t bh1750_get_data(void)
{
    BH1750_get_lumen(&bh1750_dev);
    lx_Data = bh1750_dev.value;
    return lx_Data;
}

static int bh1750_test(int argc, char **argv)
{
    PRINTF("\r\n---------光照传感器测试--------------- \r\n");
    bh1750_dev.value = 0;
    BH1750_get_lumen(&bh1750_dev);
    PRINTF("\r\n* lx: %ld  \r\n", bh1750_dev.value);
    if (bh1750_dev.value == 0)
    {
        PRINTF("\r\n -----------BH1750 FAIL------------------- \r\n");
        return 0;
    }
    PRINTF("\r\n -----------BH1750 PASS------------------- \r\n");
    return 1;
}

void BH1750_cmd_register()
{
    const cmd_item_t bh1750cmd = {
        .command = "1",
        .help = " 光照强度传感器 ",
        .func = &bh1750_test,
        .success = 0,
        .fail = 0,
    };

    console_cmd_register((cmd_item_t *)&bh1750cmd);
}
