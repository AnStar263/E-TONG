#include "ble_ssp.h"
#include "main.h"
#include "UI.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

extern volatile uint8_t rx_data;


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

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        /* 1. 处理接收到的字节 rx_data */
        // 例：写入环形缓冲区或解析协议
        switch (rx_data) {
          case 'T': console_print("BLE RE OK!"); break;         //收发测试
          case 'M': page_param();                                                  break;         // 参数页
          case 'W': page_wave();                                                   break;         // 波形页
          case 'C': page_console();                                                    break; 
          case 'P':                                                   break;         // 上一页
          case 'N':                                                    break;         // 下一页
          case '1': y_zoom_in(1);                                                               break;         // Y轴 x1
          case '2': y_zoom_in(2);                                                               break;         // Y轴 x2
          case '4': y_zoom_in(4);                                                               break;         // Y轴 x4
          case '8': y_zoom_in(8);                                                               break;         // Y轴 x8
          case '+': x_zoom_in();                                                                break;         // X轴放大
          case '-': x_zoom_out();                                                               break;         // X轴缩小
          case 'L': x_shift_left();                                                             break;         // X轴左移
          case 'R': x_shift_right();                                                            break;         // X轴右移
          case '0': reset_view();                                                               break;         // 复位缩放与平移
          case 'S': run_stop_toggle();                                                          break;         // 暂停/继续采样
      }
      /* 2. 关键：HAL 的接收中断是一次性的，必须再次调用启动下一次接收 */
      HAL_UART_Receive_IT(huart, &rx_data, 1);
    }
}

// 2. 核心补救：增加串口错误回调，一旦蓝牙连上报 ORE 溢出，立刻自动清除恢复！
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        __HAL_UART_CLEAR_OREFLAG(huart); // 清除溢出标志位
        huart->ErrorCode = HAL_UART_ERROR_NONE;
        // 重新开启中断接收
        HAL_UART_Receive_IT(huart, (uint8_t *)&rx_data, 1);
    }
}