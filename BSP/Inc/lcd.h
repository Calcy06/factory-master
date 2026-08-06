#ifndef LCD_H
#define LCD_H

#include <stdbool.h>

#include "main.h"

#define LCD_BUFFER_LEN 20
#define LCD_REPLAY_LEN 4

extern uint8_t lcd_buffer[LCD_BUFFER_LEN];
extern uint8_t lcd_count; // 计数位
extern uint8_t lcd_temp;
extern bool lcd_frame_ready;

uint8_t lcd_check_frame(void);
void lcd_cmd_register(void);
void lcd_switch(int lcd_page);
void lcd_set_data(char *buffer);

#endif
