#include "driver_st7789.h"
#include "driver_st7789_font.h"

/* 核心命令定义 (精简自原版) */
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

/* GPIO 电平控制辅助宏 */
#define ST7789_DC_CMD()     HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET)
#define ST7789_DC_DATA()    HAL_GPIO_WritePin(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET)
#define ST7789_RST_LOW()    HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_RESET)
#define ST7789_RST_HIGH()   HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_SET)
#define ST7789_CS_LOW()     HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET)
#define ST7789_CS_HIGH()    HAL_GPIO_WritePin(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET)

/**
 * @brief 写单字节命令 (已移除 CS 操作)
 */
uint8_t st7789_write_cmd(uint8_t cmd)
{
    ST7789_DC_CMD();
    ST7789_CS_LOW();
    uint8_t ret = (HAL_SPI_Transmit(ST7789_SPI_HANDLE, &cmd, 1, 100) == HAL_OK) ? 0 : 1;
    ST7789_CS_HIGH();
    return ret;
}

uint8_t st7789_write_data(uint8_t data)
{
    ST7789_DC_DATA();
    ST7789_CS_LOW();
    uint8_t ret = (HAL_SPI_Transmit(ST7789_SPI_HANDLE, &data, 1, 100) == HAL_OK) ? 0 : 1;
    ST7789_CS_HIGH();
    return ret;
}

static uint8_t a_st7789_write_bytes(uint8_t *data, uint16_t len)
{
    ST7789_DC_DATA();
    ST7789_CS_LOW();
    uint8_t ret = (HAL_SPI_Transmit(ST7789_SPI_HANDLE, data, len, 1000) == HAL_OK) ? 0 : 1;
    ST7789_CS_HIGH();
    return ret;
}

/**
 * @brief 开启显示
 */
uint8_t st7789_display_on(void)
{
    return st7789_write_cmd(ST7789_CMD_DISPON);
}

/**
 * @brief 关闭显示
 */
uint8_t st7789_display_off(void)
{
    return st7789_write_cmd(ST7789_CMD_DISPOFF);
}

/**
 * @brief 设置显示窗口 (整合原版 set_column_address 与 set_row_address)
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
 * @brief 初始化 ST7789
 */
/**
 * @brief 初始化 ST7789 (改为软件复位)
 */
uint8_t st7789_init(void)
{
    ST7789_RST_HIGH();
    HAL_Delay(10);
    ST7789_RST_LOW();
    HAL_Delay(50);
    ST7789_RST_HIGH();
    HAL_Delay(120);
    /* 硬件复位与退出睡眠时序... */
    st7789_write_cmd(ST7789_CMD_SLPOUT);
    HAL_Delay(120);

    /* 3. 关键：设置横屏模式 (开启 MV 位交换行列) */
    st7789_write_cmd(ST7789_CMD_MADCTL);
    st7789_write_data(0x60);  // 若画面上下颠倒或左右镜像，换成 0xA0

    /* 4. 设置像素格式 16-bit RGB565 */
    st7789_write_cmd(ST7789_CMD_COLMOD);
    st7789_write_data(0x55);

    /* 5. 颜色反转与显示开 */
    st7789_write_cmd(ST7789_CMD_INVON);
    st7789_write_cmd(ST7789_CMD_NORON);
    HAL_Delay(10);

    /* 6. 全屏清屏并开启显示 */
    st7789_clear(ST7789_COLOR_BLACK);
    st7789_display_on();

    return 0;
}/**
 * @brief 填充矩形区域 (RGB565)
 */
uint8_t st7789_fill_rect(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom, uint16_t color)
{
    uint32_t i, m, n, total_bytes;

    if ((left >= ST7789_COLUMN) || (right >= ST7789_COLUMN) || (left > right)) return 1;
    if ((top >= ST7789_ROW) || (bottom >= ST7789_ROW) || (top > bottom)) return 1;

    if (st7789_set_window(left, top, right, bottom) != 0) return 1;

    for (i = 0; i < ST7789_BUFFER_SIZE; i += 2)
    {
        gs_st7789_buf[i]     = (color >> 8) & 0xFF;
        gs_st7789_buf[i + 1] = color & 0xFF;
    }

    total_bytes = (uint32_t)(right - left + 1) * (bottom - top + 1) * 2;
    m = total_bytes / ST7789_BUFFER_SIZE;
    n = total_bytes % ST7789_BUFFER_SIZE;

    for (i = 0; i < m; i++)
    {
        if (a_st7789_write_bytes(gs_st7789_buf, ST7789_BUFFER_SIZE) != 0) return 1;
    }
    if (n != 0)
    {
        if (a_st7789_write_bytes(gs_st7789_buf, n) != 0) return 1;
    }

    return 0;
}

/**
 * @brief 全屏清屏
 */
uint8_t st7789_clear(uint16_t color)
{
    return st7789_fill_rect(0, 0, ST7789_COLUMN - 1, ST7789_ROW - 1, color);
}

/**
 * @brief 画单个像素点
 */
uint8_t st7789_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t data[2];

    if ((x >= ST7789_COLUMN) || (y >= ST7789_ROW)) return 1;
    if (st7789_set_window(x, y, x, y) != 0) return 1;

    data[0] = (color >> 8) & 0xFF;
    data[1] = color & 0xFF;

    return a_st7789_write_bytes(data, 2);
}

/**
 * @brief 绘制 16位 RGB565 图片
 */
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

/**
 * @brief 内部画字符函数 (保留自原版逻辑)
 */
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

/**
 * @brief 显示 ASCII 字符串
 */
uint8_t st7789_write_string(uint16_t x, uint16_t y, char *str, uint16_t len, uint16_t color, st7789_font_t font)
{
    if ((x >= ST7789_COLUMN) || (y >= ST7789_ROW)) return 1;

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