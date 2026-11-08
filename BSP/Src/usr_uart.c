#include "usr_uart.h"

uint8_t dataRcvd;

extern UART_HandleTypeDef huart1;

HAL_StatusTypeDef UART_senddBuff(UART_HandleTypeDef *huart, const uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    return (HAL_UART_Transmit(huart, pData, Size, Timeout));
}

int fputc(int ch, FILE *f)
{
    UART_senddBuff(&huart1, (uint8_t *)&ch, 1, 5);
}
