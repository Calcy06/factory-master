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

#define CMD_POWER_DOWN 0x00
#define CMD_POWER_ON 0x01
#define CMD_RESET 0x03
#define CMD_H_RES_MODE 0x10
#define CMD_H_RES_MODE2 0x11
#define CMD_L_RES_MODE 0x13
#define CMD_ONE_H_RES_MODE 0x20
#define CMD_ONE_H_RES_MODE2 0x21
#define CMD_ONE_L_RES_MODE 0x23
#define CMD_CNG_TIME_HIGH 0x30 // 3 LSB set time
#define CMD_CNG_TIME_LOW 0x60  // 5 LSB set time

uint16_t lx_Data = -1;

typedef struct BH1750_device
{
    char name[10];

    I2C_HandleTypeDef *i2c_handle;
    uint8_t address_r;
    uint8_t address_w;

    uint16_t value;

    uint8_t buffer[2];

} BH1750_device_t;

extern I2C_HandleTypeDef hi2c3;
static BH1750_device_t bh7150_dev = {0};

static HAL_StatusTypeDef BH1750_send_command(BH1750_device_t *dev, uint8_t cmd)
{
    // TODO hal checks
    if (HAL_I2C_Master_Transmit(
            dev->i2c_handle, // I2C Handle
            dev->address_w,  // I2C addr of dev
            &cmd,            // CMD to be executed
            1,               // 8bit addr
            10               // Wait time
            ) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}

static void BH1750_init_dev_struct(BH1750_device_t *dev, I2C_HandleTypeDef *i2c_handle,
                                   char *name, uint8_t addr_grounded)
{
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
    dev->i2c_handle = i2c_handle;

    strcpy(dev->name, name);
}

static HAL_StatusTypeDef BH1750_init_dev(BH1750_device_t *dev)
{
    BH1750_send_command(dev, CMD_POWER_ON);
    BH1750_send_command(dev, CMD_RESET);
    BH1750_send_command(dev, CMD_H_RES_MODE);

    return HAL_OK;
}

static HAL_StatusTypeDef BH1750_get_lumen(BH1750_device_t *dev)
{
    if (HAL_I2C_Master_Receive(dev->i2c_handle,
                               dev->address_r,
                               dev->buffer,
                               2,
                               10) != HAL_OK)
        return HAL_ERROR;

    /* 按照数据手册计算 */
    dev->value = dev->buffer[0];
    dev->value = (dev->value << 8) | dev->buffer[1];

    // TODO check float stuff
    dev->value /= 1.2;

    return HAL_OK;
}

uint16_t bh7150_get_data(void)
{
    BH1750_get_lumen(&bh7150_dev);
    lx_Data = bh7150_dev.value;
    return bh7150_dev.value;
}

static int bh7150_read(int argc, char **argv)
{
    /* read bh7150 data ，please wait 2000ms */
    PRINTF("\r\n---------光照传感器测试--------------- \r\n");
    bh7150_dev.value = -1;
    BH1750_get_lumen(&bh7150_dev);
    lx_Data = bh7150_dev.value;
    PRINTF("\r\n* lx: %ld  \r\n", bh7150_dev.value);
    if (bh7150_dev.value == -1)
    {
        PRINTF("\r\n -----------BH1750 FAIL------------------- \r\n");
        return 0;
    }
    PRINTF("\r\n -----------BH1750 PASS------------------- \r\n");
    return 1;
}

void BH7150_cmd_register(void)
{
    const cmd_item_t bh7150cmd = {
        .command = "1",
        .help = " 光照强度传感器 ",
        .func = &bh7150_read,
        .success = 0,
        .fail = 0,
    };

    console_cmd_register((cmd_item_t *)&bh7150cmd);
}

void BH1750_INIT(void)
{
    BH1750_init_dev_struct(&bh7150_dev, &hi2c3, "BH1750", true);
    BH1750_init_dev(&bh7150_dev);
}
