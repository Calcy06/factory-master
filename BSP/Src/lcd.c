#include "lcd.h"

#include "main.h"
#include "console.h"

uint8_t lcd_buffer[LCD_BUFFER_LEN];
uint8_t lcd_count;     // 计数位
uint8_t lcd_temp;      // 缓存字符
bool lcd_ready = true; // lcd 就绪标志

extern UART_HandleTypeDef huart3;

#define LCD_REPLAY "OK\r\n"

char *strArray[15] = {0}; // 指针数组
int gn_count = 1;

// 检查报文帧的开头
uint8_t lcd_check_frame(void)
{
    // 字符串比较，看看是不是我要的那一帧
    if (0 == (memcmp(&lcd_buffer, LCD_REPLAY, 4)))
    {
        lcd_ready = true;
        memset(lcd_buffer, 0, lcd_count);
        lcd_count = 0;
        lcd_temp = 0;
        return 1;
    }
    memset(lcd_buffer, 0, lcd_count);
    lcd_count = 0;
    lcd_temp = 0;

    return 0;
}

void lcd_test()
{
    char temp[100] = {0};
    sprintf(temp, "SET_TXT(4,'99.9\xA1\xE3\x43');SET_TXT(5,'99.9RH');SET_TXT(6,'999lx');\r\n");

    PRINTF("\r\n------------LCD显示屏测试--------------- \r\n");
    memset(lcd_buffer, 0, lcd_count);
    lcd_ready = false;
    memset(lcd_buffer, 0, LCD_BUFFER_LEN);
    lcd_count = 0;
    lcd_temp = 0;
    HAL_UART_Transmit(&huart3, (const uint8_t *)temp, strlen(temp), 100);
    HAL_Delay(2000);

    if (lcd_ready)
    {
        PRINTF("\r\n -----------LCD PASS------------------- \r\n");
        return 1;
    }

    PRINTF("\r\n -----------LCD FAIL------------------- \r\n");

    return 0;
}

void lcd_cmd_register()
{
    const cmd_item_t cmd = {
        .command = "7",
        .help = " lcd卫星",
        .func = &lcd_test,
        .success = 0,
        .fail = 0,
    };

    console_cmd_register((cmd_item_t *)&cmd);
}

// 翻到哪一页
void lcd_switch(int lcd_page)
{
    char temp[128] = {0};

    /* 等待LCD就绪 */
    if (!lcd_ready)
    {
        // 没准备好，可能丢包了，也可能没到，延时一段时间，直接发就好
        HAL_Delay(500);
    }

    sprintf(temp, "JUMP(%d);\r\n", lcd_page);
    HAL_UART_Transmit(&huart3, (const uint8_t *)temp, strlen(temp), 100);
    lcd_ready = false;
}

// 填什么内容
void lcd_set_data(char *buffer)
{
    if (!lcd_ready) // 屏未就绪则先延时
    {
        HAL_Delay(500);
    }
    HAL_UART_Transmit(&huart3, (const uint8_t *)buffer, strlen(buffer), 100); // 下发文本帧
    lcd_ready = false;                                                        // 标记忙，等屏回 OK
}
