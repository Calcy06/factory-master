#ifndef GNSS_H
#define GNSS_H

#include <stdbool.h>

#include "main.h"

#define GNSS_BUFFER_LEN 1024
#define GNSS_FRAME_LEN 20
#define GNSS_DATA_HEAD_LEN 7

extern uint8_t gnss_buffer[GNSS_BUFFER_LEN];
extern uint8_t gnss_count; // 计数位
extern uint8_t gnss_temp;
extern bool gnss_frame_ready;

uint8_t gnss_check_frame(void);
void gnss_deal_buffer(void);
void gnss_cmd_register(void);

#endif
