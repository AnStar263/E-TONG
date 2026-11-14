#include "ble_ssp.h"
#include "main.h"
#include "UI.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

extern volatile uint8_t rx_data;
extern volatile PAGE current_page;

volatile PAGE next_page_req = PAGE_CONSOLE;
volatile uint8_t page_change_flag = 0;
volatile uint8_t wave_div_change_flag = 0;

uint8_t JDY33_SendCmd(UART_HandleTypeDef *huart, const char *cmd, const char *expected_reply, uint32_t timeout_ms)
{
    uint8_t rx_buf[64] = {0};
    __HAL_UART_FLUSH_DRREGISTER(huart);

    if (HAL_UART_Transmit(huart, (uint8_t *)cmd, strlen(cmd), 200) != HAL_OK) {
        return 0;
    }

    HAL_UART_Receive(huart, rx_buf, sizeof(rx_buf) - 1, timeout_ms);

    if (strstr((char *)rx_buf, expected_reply) != NULL) {
        return 1;
    }
    return 0;
}

uint8_t JDY33_SetBaudRate(UART_HandleTypeDef *huart, uint32_t target_baud)
{
    const char *baud_cmd = NULL;

    switch (target_baud) {
        case 9600:   baud_cmd = "AT+BAUD4\r\n"; break;
        case 19200:  baud_cmd = "AT+BAUD5\r\n"; break;
        case 38400:  baud_cmd = "AT+BAUD6\r\n"; break;
        case 57600:  baud_cmd = "AT+BAUD7\r\n"; break;
        case 115200: baud_cmd = "AT+BAUD8\r\n"; break;
        case 128000: baud_cmd = "AT+BAUD9\r\n"; break;
        default:     return 0;
    }

    if (JDY33_SendCmd(huart, "AT\r\n", "+OK", 100)) {
        if (huart->Init.BaudRate == target_baud) {
            return 1;
        }
    }

    if (!JDY33_SendCmd(huart, baud_cmd, "+OK", 300)) {
        return 0;
    }

    HAL_Delay(100);

    huart->Init.BaudRate = target_baud;
    if (HAL_UART_Init(huart) != HAL_OK) {
        return 0;
    }

    if (JDY33_SendCmd(huart, "AT\r\n", "+OK", 200)) {
        return 1;
    }
    return 0;
}

/**
 * @brief 向上位机回传测量参数 (匹配 MainActivity.java 的 vavg,vpp,vmax,freq 格式)
 */
void BLE_SendParams(UART_HandleTypeDef *huart, const volatile MeasureParams_t *params)
{
    char tx_buf[64];
    int len = snprintf(tx_buf, sizeof(tx_buf), "%.3f,%.3f,%.3f,%.1f\r\n",
                       params->vavg, params->vpp, params->vmax, params->freq);
    if (len > 0) {
        HAL_UART_Transmit(huart, (uint8_t *)tx_buf, (uint16_t)len, 20);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        switch (rx_data) {
          // Y轴垂直分辨率
          case '1': set_y_scale(1); wave_div_change_flag = 1; break;
          case '2': set_y_scale(2); wave_div_change_flag = 1; break;
          case '4': set_y_scale(4); wave_div_change_flag = 1; break;
          case '8': set_y_scale(8); wave_div_change_flag = 1; break;

          // X轴时基按键
          case '+': x_zoom_in();  break;
          case '-': x_zoom_out(); break;
          case '0': reset_view(); wave_div_change_flag = 1; break;

          // 页面直接切换与上下翻页 (匹配 App 的 W/M/C/P/N 指令)
          case 'W': next_page_req = PAGE_WAVE;    page_change_flag = 1; break;
          case 'M': next_page_req = PAGE_PARAM;   page_change_flag = 1; break;
          case 'C': next_page_req = PAGE_CONSOLE; page_change_flag = 1; break;
          case 'P':
            if (current_page == PAGE_WAVE)         next_page_req = PAGE_CONSOLE;
            else if (current_page == PAGE_PARAM)   next_page_req = PAGE_WAVE;
            else                                   next_page_req = PAGE_PARAM;
            page_change_flag = 1;
            break;
          case 'N':
            if (current_page == PAGE_WAVE)         next_page_req = PAGE_PARAM;
            else if (current_page == PAGE_PARAM)   next_page_req = PAGE_CONSOLE;
            else                                   next_page_req = PAGE_WAVE;
            page_change_flag = 1;
            break;

          case 'S': run_stop_toggle(); break;

          // X轴平移控制
          case 'L': x_shift_left();  break;
          case 'R': x_shift_right(); break;
        }

        HAL_UART_Receive_IT(huart, (uint8_t *)&rx_data, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        huart->ErrorCode = HAL_UART_ERROR_NONE;
        HAL_UART_Receive_IT(huart, (uint8_t *)&rx_data, 1);
    }
}