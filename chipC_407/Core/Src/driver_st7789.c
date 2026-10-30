#include "driver_st7789.h"
#include "driver_st7789_font.h"
#include <string.h>

/* 核心命令定义 */
#define ST7789_CMD_SWRESET         0x01
#define ST7789_CMD_SLPIN           0x10
#define ST7789_CMD_SLPOUT          0x11
#define ST7789_CMD_NORON           0x13
#define ST7789_CMD_INVOFF          0x20
#define ST7789_CMD_INVON           0x21
#define ST7789_CMD_DISPOFF         0x28
#define ST7789_CMD_DISPON          0x29
#define ST7789_CMD_CASET           0x2A
#define ST7789_CMD_RASET           0x2B
#define ST7789_CMD_RAMWR           0x2C
#define ST7789_CMD_MADCTL          0x36
#define ST7789_CMD_COLMOD          0x3A

/* 内部显存发送缓冲区 */
static uint8_t gs_st7789_buf[ST7789_BUFFER_SIZE];

/* GPIO 控制宏 */
#define ST7789_DC_CMD()     HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET)
#define ST7789_DC_DATA()    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET)
#define ST7789_RST_LOW()    HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_RESET)
#define ST7789_RST_HIGH()   HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_SET)
#define ST7789_CS_LOW()     HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET)
#define ST7789_CS_HIGH()    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET)

/**
 * @brief 写单字节命令（使用标准安全 HAL 发送）
 */
uint8_t st7789_write_cmd(uint8_t cmd)
{
    ST7789_DC_CMD();
    ST7789_CS_LOW();
    uint8_t ret = (HAL_SPI_Transmit(ST7789_SPI_HANDLE, &cmd, 1, 100) == HAL_OK) ? 0 : 1;
    ST7789_CS_HIGH();
    return ret;
}

/**
 * @brief 写单字节数据
 */
uint8_t st7789_write_data(uint8_t data)
{
    ST7789_DC_DATA();
    ST7789_CS_LOW();
    uint8_t ret = (HAL_SPI_Transmit(ST7789_SPI_HANDLE, &data, 1, 100) == HAL_OK) ? 0 : 1;
    ST7789_CS_HIGH();
    return ret;
}

/**
 * @brief 连续写多字节
 */
static uint8_t a_st7789_write_bytes(uint8_t *data, uint16_t len)
{
    ST7789_DC_DATA();
    ST7789_CS_LOW();
    uint8_t ret = (HAL_SPI_Transmit(ST7789_SPI_HANDLE, data, len, 1000) == HAL_OK) ? 0 : 1;
    ST7789_CS_HIGH();
    return ret;
}

uint8_t st7789_display_on(void)
{
    return st7789_write_cmd(ST7789_CMD_DISPON);
}

uint8_t st7789_display_off(void)
{
    return st7789_write_cmd(ST7789_CMD_DISPOFF);
}

/**
 * @brief 设置显示窗口
 */
uint8_t st7789_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    uint8_t buf[4];

    /* 设置列地址 */
    if (st7789_write_cmd(ST7789_CMD_CASET) != 0) return 1;
    buf[0] = (x_start >> 8) & 0xFF;
    buf[1] = x_start & 0xFF;
    buf[2] = (x_end >> 8) & 0xFF;
    buf[3] = x_end & 0xFF;
    if (a_st7789_write_bytes(buf, 4) != 0) return 1;

    /* 设置行地址 */
    if (st7789_write_cmd(ST7789_CMD_RASET) != 0) return 1;
    buf[0] = (y_start >> 8) & 0xFF;
    buf[1] = y_start & 0xFF;
    buf[2] = (y_end >> 8) & 0xFF;
    buf[3] = y_end & 0xFF;
    if (a_st7789_write_bytes(buf, 4) != 0) return 1;

    /* 准备写显存 */
    return st7789_write_cmd(ST7789_CMD_RAMWR);
}

/**
 * @brief 初始化 ST7789（加入显式 SPI 使能）
 */
uint8_t st7789_init(void)
{
    // 显式开启 SPI 硬件，防止寄存器状态异常
    __HAL_SPI_ENABLE(ST7789_SPI_HANDLE);

    ST7789_RST_HIGH();
    HAL_Delay(10);
    ST7789_RST_LOW();
    HAL_Delay(50);
    ST7789_RST_HIGH();
    HAL_Delay(120);

    st7789_write_cmd(ST7789_CMD_SLPOUT);
    HAL_Delay(120);

    /* 设置横屏模式 */
    st7789_write_cmd(ST7789_CMD_MADCTL);
    st7789_write_data(0x60);  // 若画面上下颠倒或左右镜像，换成 0xA0

    /* 设置像素格式 16-bit RGB565 */
    st7789_write_cmd(ST7789_CMD_COLMOD);
    st7789_write_data(0x55);

    /* 颜色反转与显示开 */
    st7789_write_cmd(ST7789_CMD_INVON);
    st7789_write_cmd(ST7789_CMD_NORON);
    HAL_Delay(10);

    /* 全屏清屏并开启显示 */
    st7789_clear(ST7789_COLOR_BLACK);
    st7789_display_on();

    return 0;
}

/**
 * @brief 填充矩形区域（单行批量连续发送，消除 0.5 秒切页延迟）
 */
uint8_t st7789_fill_rect(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom, uint16_t color)
{
    if ((left >= ST7789_COLUMN) || (right >= ST7789_COLUMN) || (left > right)) return 1;
    if ((top >= ST7789_ROW) || (bottom >= ST7789_ROW) || (top > bottom)) return 1;

    if (st7789_set_window(left, top, right, bottom) != 0) return 1;

    uint16_t width = right - left + 1;
    uint16_t height = bottom - top + 1;

    // 准备一行像素缓冲 (320 像素最大占 640 字节，STM32F407 栈/静态内存极为充沛)
    static uint8_t line_buffer[640];
    uint16_t line_bytes = width * 2;
    for (uint16_t i = 0; i < line_bytes; i += 2)
    {
        line_buffer[i]     = (color >> 8) & 0xFF;
        line_buffer[i + 1] = color & 0xFF;
    }

    // 保持 CS 为低，逐行整块推送，免除频繁进出中断与 CS 抖动开销
    ST7789_DC_DATA();
    ST7789_CS_LOW();
    for (uint16_t row = 0; row < height; row++)
    {
        HAL_SPI_Transmit(ST7789_SPI_HANDLE, line_buffer, line_bytes, 100);
    }
    ST7789_CS_HIGH();

    return 0;
}

uint8_t st7789_clear(uint16_t color)
{
    return st7789_fill_rect(0, 0, ST7789_COLUMN - 1, ST7789_ROW - 1, color);
}

/**
 * @brief 极速单点绘制（优化片选周期，为 30 FPS 实时波形提速）
 */
uint8_t st7789_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    if ((x >= ST7789_COLUMN) || (y >= ST7789_ROW)) return 1;

    uint8_t cmd_caset = ST7789_CMD_CASET;
    uint8_t x_buf[4] = { (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)x };
    uint8_t cmd_raset = ST7789_CMD_RASET;
    uint8_t y_buf[4] = { (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y >> 8), (uint8_t)y };
    uint8_t cmd_ramwr = ST7789_CMD_RAMWR;
    uint8_t color_buf[2] = { (uint8_t)(color >> 8), (uint8_t)color };

    // 整个写点流程锁定在单个 CS 周期内完成
    ST7789_CS_LOW();

    ST7789_DC_CMD();
    HAL_SPI_Transmit(ST7789_SPI_HANDLE, &cmd_caset, 1, 10);
    ST7789_DC_DATA();
    HAL_SPI_Transmit(ST7789_SPI_HANDLE, x_buf, 4, 10);

    ST7789_DC_CMD();
    HAL_SPI_Transmit(ST7789_SPI_HANDLE, &cmd_raset, 1, 10);
    ST7789_DC_DATA();
    HAL_SPI_Transmit(ST7789_SPI_HANDLE, y_buf, 4, 10);

    ST7789_DC_CMD();
    HAL_SPI_Transmit(ST7789_SPI_HANDLE, &cmd_ramwr, 1, 10);
    ST7789_DC_DATA();
    HAL_SPI_Transmit(ST7789_SPI_HANDLE, color_buf, 2, 10);

    ST7789_CS_HIGH();
    return 0;
}

uint8_t st7789_draw_picture_16bits(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom, const uint16_t *image)
{
    uint32_t i, j, m, n, total_bytes;
    uint32_t point = 0;
    uint16_t color;

    if ((left >= ST7789_COLUMN) || (right >= ST7789_COLUMN) || (left > right)) return 1;
    if ((top >= ST7789_ROW) || (bottom >= ST7789_ROW) || (top > bottom)) return 1;

    if (st7789_set_window(left, top, right, bottom) != 0) return 1;

    total_bytes = (uint32_t)(right - left + 1) * (bottom - top + 1) * 2;
    m = total_bytes / ST7789_BUFFER_SIZE;
    n = total_bytes % ST7789_BUFFER_SIZE;

    for (i = 0; i < m; i++)
    {
        for (j = 0; j < ST7789_BUFFER_SIZE; j += 2)
        {
            color = image[point++];
            gs_st7789_buf[j]     = (color >> 8) & 0xFF;
            gs_st7789_buf[j + 1] = color & 0xFF;
        }
        if (a_st7789_write_bytes(gs_st7789_buf, ST7789_BUFFER_SIZE) != 0) return 1;
    }
    if (n != 0)
    {
        for (j = 0; j < n; j += 2)
        {
            color = image[point++];
            gs_st7789_buf[j]     = (color >> 8) & 0xFF;
            gs_st7789_buf[j + 1] = color & 0xFF;
        }
        if (a_st7789_write_bytes(gs_st7789_buf, n) != 0) return 1;
    }

    return 0;
}

static uint8_t a_st7789_show_char(uint16_t x, uint16_t y, uint8_t chr, uint8_t size, uint16_t color)
{
    uint8_t temp, t, t1;
    uint16_t y0 = y;
    uint8_t csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2);

    chr = chr - ' ';
    for (t = 0; t < csize; t++)
    {
        if (size == 12)      temp = gsc_st7789_ascii_1206[chr][t];
        else if (size == 16) temp = gsc_st7789_ascii_1608[chr][t];
        else if (size == 24) temp = gsc_st7789_ascii_2412[chr][t];
        else return 1;

        for (t1 = 0; t1 < 8; t1++)
        {
            if ((temp & 0x80) != 0)
            {
                if (st7789_draw_point(x, y, color) != 0) return 1;
            }
            temp <<= 1;
            y++;
            if ((y - y0) == size)
            {
                y = y0;
                x++;
                break;
            }
        }
    }
    return 0;
}

uint8_t st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, st7789_font_t font)
{
    if ((x >= ST7789_COLUMN) || (y >= ST7789_ROW) || (str == NULL)) return 1;

    uint16_t len = strlen(str);

    while ((len != 0) && (*str <= '~') && (*str >= ' '))
    {
        if (x >= (ST7789_COLUMN - (font / 2)))
        {
            x = 0;
            y += (uint8_t)font;
        }
        if (y >= (ST7789_ROW - font))
        {
            y = x = 0;
        }
        if (a_st7789_show_char(x, y, *str, font, color) != 0) return 1;

        x += (uint8_t)(font / 2);
        str++;
        len--;
    }
    return 0;
}