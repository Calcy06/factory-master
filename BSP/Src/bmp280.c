#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bmp280.h"

#include "main.h"
#include "console.h"

static uint8_t gn_buffer[BMP_BUFFER_LEN];
uint8_t bmp280_buffer[BMP_BUFFER_LEN];
uint8_t bmp280_count;            // 计数位
uint8_t bmp280_temp;             // 缓存字符
bool bmp280_frame_ready = false; // 帧就绪标志
static bool receive_success = false;

extern UART_HandleTypeDef huart10;

int pressure_Data = -1;
double height_Data = -1;
#define BMP280_HEAD "Pressure"

static void get_pressure_number(char *input, int *pressure_Data, double *height_Data)
{
    char *token;
    int num = 0;
    // 第一次调用strtok，以空格为分隔符
    token = strtok(input, " ");
    while (token != NULL)
    {
        // 如果是数字则转换并终止循环
        if (num == 2)
        {
            *pressure_Data = atoi(token);
        }

        if (num == 5)
        {
            *height_Data = atof(token);
        }

        num++;
        token = strtok(NULL, " ");
    }
}

void bmp280_deal_buffer(void)
{
    /* 获取第一个子字符串 */
    char buffer[128] = {0};
    memset(gn_buffer, 0, BMP_BUFFER_LEN);
    memcpy(gn_buffer, bmp280_buffer, bmp280_count);
    memcpy(buffer, bmp280_buffer, bmp280_count);
    memset(bmp280_buffer, 0, bmp280_count);

    get_pressure_number(buffer, &pressure_Data, &height_Data);
    bmp280_count = 0;
    bmp280_temp = 0;
    bmp280_frame_ready = false;
}

static int bmp280_test()
{
    PRINTF("\r\n---------大气压传感器测试--------------- \r\n");

    printf("\r\n* %s\r\n", gn_buffer);
    if (receive_success)
    {
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
