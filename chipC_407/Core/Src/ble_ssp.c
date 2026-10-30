#include "ble_ssp.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/**
 * @brief  向 JDY-33 发送 AT 指令并等待匹配预期的应答
 * @param  huart: 串口句柄指针
 * @param  cmd: AT 指令字符串 (必须带 \r\n)
 * @param  expected_reply: 期望收到的应答子串 (如 "+OK")
 * @param  timeout_ms: 等待超时时间 (ms)
 * @retval 1: 成功匹配, 0: 超时或应答错误
 */
uint8_t JDY33_SendCmd(UART_HandleTypeDef *huart, const char *cmd, const char *expected_reply, uint32_t timeout_ms)
{
    uint8_t rx_buf[64] = {0};
    
    // 清空单片机串口硬件接收 FIFO，防止残留数据干扰
    __HAL_UART_FLUSH_DRREGISTER(huart);

    // 1. 发送 AT 指令
    if (HAL_UART_Transmit(huart, (uint8_t *)cmd, strlen(cmd), 200) != HAL_OK) {
        return 0;
    }

    // 2. 阻塞接收模块响应（JDY-33 响应通常很短，几十字节足够）
    // 采用逐字节接收或一次性接收并超时判据
    HAL_UART_Receive(huart, rx_buf, sizeof(rx_buf) - 1, timeout_ms);

    // 3. 校验应答
    if (strstr((char *)rx_buf, expected_reply) != NULL) {
        return 1;
    }

    return 0;
}

/**
 * @brief  配置 JDY-33 目标波特率并同步修改单片机串口波特率
 * @param  huart: 串口句柄指针 (如 &huart1)
 * @param  target_baud: 目标波特率 (例如 115200)
 * @retval 1: 配置成功, 0: 配置失败
 */
uint8_t JDY33_SetBaudRate(UART_HandleTypeDef *huart, uint32_t target_baud)
{
    const char *baud_cmd = NULL;

    // 映射 JDY-33 AT+BAUD 指令参数
    switch (target_baud) {
        case 9600:   baud_cmd = "AT+BAUD4\r\n"; break;
        case 19200:  baud_cmd = "AT+BAUD5\r\n"; break;
        case 38400:  baud_cmd = "AT+BAUD6\r\n"; break;
        case 57600:  baud_cmd = "AT+BAUD7\r\n"; break;
        case 115200: baud_cmd = "AT+BAUD8\r\n"; break;
        case 128000: baud_cmd = "AT+BAUD9\r\n"; break;
        default:     return 0; // 不支持的波特率
    }

    // 步骤 1：先使用单片机当前波特率向模块发握手测试
    // 若 JDY-33 已经处于目标波特率，直接返回成功，避免重复写入 Flash
    if (JDY33_SendCmd(huart, "AT\r\n", "+OK", 100)) {
        if (huart->Init.BaudRate == target_baud) {
            return 1; // 已经是目标波特率且握手正常
        }
    }

    // 步骤 2：下发修改波特率指令
    if (!JDY33_SendCmd(huart, baud_cmd, "+OK", 300)) {
        // 如果没收到 +OK，可能是模块处于透传状态（蓝牙已连接）或通信速率不匹配
        return 0;
    }

    // 步骤 3：延时等待 JDY-33 内部写入 Flash 并重启波特率引擎
    HAL_Delay(100);

    // 步骤 4：单片机端重新初始化串口至目标波特率
    huart->Init.BaudRate = target_baud;
    if (HAL_UART_Init(huart) != HAL_OK) {
        return 0;
    }

    // 步骤 5：以新波特率发送 "AT\r\n" 进行验证
    if (JDY33_SendCmd(huart, "AT\r\n", "+OK", 200)) {
        return 1; // 切换并验证成功
    }

    return 0;
}