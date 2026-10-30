#ifndef DRIVER_ST7789_H
#define DRIVER_ST7789_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "stm32f407xx.h"    // 直接引入 F407 芯片寄存器定义（GPIOC 定义在此处）

#ifdef __cplusplus
extern "C"{
#endif

/* ==================== STM32H743 硬件接口配置 ==================== */
extern SPI_HandleTypeDef hspi1;               /* 修改为你实际使用的 SPI 句柄 */
#define ST7789_SPI_HANDLE       (&hspi1)

#define ST7789_DC_PORT          SPI_DC_GPIO_Port
#define ST7789_DC_PIN           SPI_DC_Pin

#define ST7789_RST_PORT         SPI_RST_GPIO_Port
#define ST7789_RST_PIN          SPI_RST_Pin

#define ST7789_CS_PORT          SPI_CS_GPIO_Port
#define ST7789_CS_PIN           SPI_CS_Pin
/* 屏幕默认分辨率与发送缓冲区大小 */
#define ST7789_COLUMN           320           /* 屏幕宽度 <= 240 */
#define ST7789_ROW              240           /* 屏幕高度 <= 320 (常见为 240 或 320) */
#define ST7789_BUFFER_SIZE      4096          

/* 常用 RGB565 颜色定义 */
#define ST7789_COLOR_WHITE      0xFFFF
#define ST7789_COLOR_BLACK      0x0000
#define ST7789_COLOR_BLUE       0x001F
#define ST7789_COLOR_RED        0xF800
#define ST7789_COLOR_GREEN      0x07E0
#define ST7789_COLOR_YELLOW     0xFFE0

/**
 * @brief 字体大小枚举 (保留自原版驱动)
 */
typedef enum
{
    ST7789_FONT_12 = 0x0C,        /**< 12号字体 */
    ST7789_FONT_16 = 0x10,        /**< 16号字体 */
    ST7789_FONT_24 = 0x18,        /**< 24号字体 */
} st7789_font_t;

/**
 * @brief 显存扫描方向枚举 (MADCTL 寄存器参数)
 */
typedef enum
{
    ST7789_ORDER_PAGE_TOP_TO_BOTTOM    = (0 << 7),
    ST7789_ORDER_PAGE_BOTTOM_TO_TOP    = (1 << 7),
    ST7789_ORDER_COLUMN_LEFT_TO_RIGHT  = (0 << 6),
    ST7789_ORDER_COLUMN_RIGHT_TO_LEFT  = (1 << 6),
    ST7789_ORDER_PAGE_COLUMN_NORMAL    = (0 << 5),
    ST7789_ORDER_PAGE_COLUMN_REVERSE   = (1 << 5),
    ST7789_ORDER_LINE_TOP_TO_BOTTOM    = (0 << 4),
    ST7789_ORDER_LINE_BOTTOM_TO_TOP    = (1 << 4),
    ST7789_ORDER_COLOR_RGB             = (0 << 3),
    ST7789_ORDER_COLOR_BGR             = (1 << 3),
    ST7789_ORDER_REFRESH_LEFT_TO_RIGHT = (0 << 2),
    ST7789_ORDER_REFRESH_RIGHT_TO_LEFT = (1 << 2),
} st7789_order_t;

/* ==================== 核心驱动函数 ==================== */
uint8_t st7789_init(void);
uint8_t st7789_clear(uint16_t color);
uint8_t st7789_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
uint8_t st7789_draw_point(uint16_t x, uint16_t y, uint16_t color);
uint8_t st7789_fill_rect(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom, uint16_t color);
uint8_t st7789_draw_picture_16bits(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom, const uint16_t *image);
uint8_t st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, st7789_font_t font);
uint8_t st7789_display_on(void);
uint8_t st7789_display_off(void);
uint8_t st7789_write_cmd(uint8_t cmd);
uint8_t st7789_write_data(uint8_t data);

#ifdef __cplusplus
}
#endif

#endif