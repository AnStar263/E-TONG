#ifndef __BLE_SSP_H
#define __BLE_SSP_H

#include "main.h"
uint8_t JDY33_SendCmd(UART_HandleTypeDef *huart, const char *cmd, const char *expected_reply, uint32_t timeout_ms);
uint8_t JDY33_SetBaudRate(UART_HandleTypeDef *huart, uint32_t target_baud);





#endif
