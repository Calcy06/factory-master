#include "EB21.h"

#include "main.h"
#include "console.h"

uint8_t xs_buffer[XS_BUFFER_LEN];
uint16_t xs_count = 0; // 计数位
uint8_t xs_temp = 0;   // 缓存字符

#define XS_TEST_CMD "AT+MAC?\r\n"

extern UART_HandleTypeDef huart7;
extern UART_HandleTypeDef huart1;

static int xs_get_msg(int argc, char **argv)
{
    printf("\r\n-----------------星闪测试-----------\r\n");
    memset(xs_buffer, 0, xs_count);
    xs_count = 0;
    xs_temp = 0;
    HAL_UART_Transmit(&huart7, XS_TEST_CMD, strlen(XS_TEST_CMD), 1000);
    HAL_Delay(1000);
    printf("\r\n* %s\r\n", XS_TEST_CMD);
    printf("\r\n* %s\r\n", xs_buffer);
    if (strlen(xs_buffer))
    {
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
