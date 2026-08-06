#ifndef BMP280_H
#define BMP280_H

#include <stdbool.h>

#include "main.h"

#define BMP_BUFFER_LEN 128
#define BMP280_HEAD_LEN 8

extern uint8_t bmp280_buffer[BMP_BUFFER_LEN];
extern uint8_t bmp280_count; // 计数位
extern uint8_t bmp280_temp;
extern bool bmp280_frame_ready;
extern double height_Data;

uint8_t bmp280_check_frame(void);
void bmp280_deal_buffer(void);
void bmp280_cmd_register(void);

#endif
