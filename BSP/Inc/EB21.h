#ifndef EB21_H
#define EB21_H

#include <stdbool.h>

#include "main.h"

#define XS_BUFFER_LEN 256

extern uint8_t xs_buffer[XS_BUFFER_LEN];
extern uint16_t xs_count; // 计数位
extern uint8_t xs_temp;

void xs_cmd_register(void);

#endif
