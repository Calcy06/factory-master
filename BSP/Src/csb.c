#include "csb.h"

#include "main.h"
#include "console.h"

uint8_t gn_buffer[CSB_BUFFER_LEN];
uint8_t csb_buffer[CSB_BUFFER_LEN];
uint8_t csb_count;            // 计数位
uint8_t csb_temp;             // 缓存字符
bool csb_frame_ready = false; // 帧就绪标志
static bool receive_success = false;
extern bool fs_status;

void csb_deal_buffer()
{
    memset(gn_buffer, 0, CSB_BUFFER_LEN);
    memcpy(gn_buffer, csb_buffer, csb_count);

    memset(csb_buffer, 0, csb_count);
    csb_count = 0;
    csb_temp = 0;

    csb_frame_ready = false;
    receive_success = true;
    int distance = (int)gn_buffer[1] << 8 | (int)gn_buffer[2];
    if (!fs_status)
    {
        if (distance < 300)
        {
            HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_RESET);
        }
    }
}

static int csb_test(int argc, char **argv)
{
    PRINTF("\r\n---------超声波传感器测试--------------- \r\n");
    int distance = gn_buffer[1] << 8 | gn_buffer[0];
    printf("\r\n*超声波测试距离：%dmm\r\n", distance);
    if (receive_success)
    {
        PRINTF("\r\n -----------超声波 PASS------------------- \r\n");

        return 1;
    }
    PRINTF("\r\n -----------超声波 FAIL------------------- \r\n");

    receive_success = false;
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
