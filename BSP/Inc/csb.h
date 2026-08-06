#ifndef CSB_H
#define CSB_H

#include <stdbool.h>

#include "main.h"

#define CSB_BUFFER_LEN 128
#define CSB_FRAME_LEN 4

extern uint8_t csb_buffer[CSB_BUFFER_LEN];
extern uint8_t csb_count; // 计数位
extern uint8_t csb_temp;
extern bool csb_frame_ready;

void csb_cmd_register(void);
void csb_deal_buffer(void);

#endif
