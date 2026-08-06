#include "gnss.h"

#include "main.h"
#include "console.h"

static uint8_t gn_buffer[GNSS_BUFFER_LEN];
uint8_t gnss_buffer[GNSS_BUFFER_LEN];
uint8_t gnss_count;            // 计数位
uint8_t gnss_temp;             // 缓存字符
bool gnss_frame_ready = false; // 帧就绪标志
static bool gnss_success = false;

extern UART_HandleTypeDef huart7;

#define GNSS_DATA_HEAD "$GNRMC,"

// 检查报文开头
uint8_t gnss_check_frame()
{
    if (memcmp(gnss_buffer, GNSS_DATA_HEAD, GNSS_DATA_HEAD_LEN))
    {
        return 1;
    }
    return 0;
}

// 处理gnss_buffer缓冲区
void gnss_deal_buffer()
{
    memset(gn_buffer, 0, GNSS_BUFFER_LEN);
    memcpy(gn_buffer, gnss_buffer, gnss_count);

    memset(gnss_buffer, 0, gnss_count);
    gnss_count = 0;
    gnss_temp = 0;
    gnss_frame_ready = false;
    gnss_success = true;
}

int gnss_test()
{
    PRINTF("-------gnss卫星测试---------");

    printf("\r\n* %s\r\n", gn_buffer);
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
