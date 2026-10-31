#include "UI.h"
#include "main.h"
#include "driver_st7789.h"
#include <stdarg.h>
#include <stdio.h>

/* --- 控制台参数配置 --- */
#define CONSOLE_FONT          ST7789_FONT_16
#define CONSOLE_FONT_W        8       // 12号字宽度为 6 像素 (12 / 2)
#define CONSOLE_FONT_H        16      // 12号字高度为 12 像素
#define CONSOLE_START_Y       28      // 内容显示起始Y坐标（避开标题与顶部分割线）
#define CONSOLE_COLOR_BG      ST7789_COLOR_BLACK
#define CONSOLE_COLOR_TXT     ST7789_COLOR_WHITE

// 计算当前屏幕区域下最大支持的行数与每行字符数 (320x240)
#define CONSOLE_MAX_COLS      (320 / CONSOLE_FONT_W)                          // 约 53 字符/行
#define CONSOLE_MAX_LINES     ((240 - CONSOLE_START_Y) / CONSOLE_FONT_H)      // 17 行

/* --- 基础界面宏定义 (320x240) --- */
#define UI_HEADER_H           24                          // 顶栏高度
#define UI_BODY_START_Y       25                          // 内容区域起始 Y 坐标
#define UI_BODY_END_Y         239                         // 内容区域结束 Y 坐标

// 垂直显示范围限制
#define WAVE_AREA_TOP       25
#define WAVE_AREA_BOTTOM    239
#define WAVE_CENTER_Y       132   // 中心零点位置

static float g_v_scale = 1.0f;    // 垂直增益
static int16_t g_y_offset = 0;    // 垂直偏移量

/* --- 参数页显示缓存 --- */
static MeasureParams_t current_params = {0};

volatile PAGE current_page = PAGE_CONSOLE;

// 文本行缓冲区
static char console_buffer[CONSOLE_MAX_LINES][CONSOLE_MAX_COLS + 1];
static uint8_t current_line = 0;

/**
 * @brief 将原始 ADC 采样值 (0~4095) 转换为屏幕 Y 坐标 (25~239)
 */
uint8_t wave_adc_to_screen_y(uint16_t adc_val)
{
    // 1. 将 0~4095 归一化到以中心点 (2048) 为基准的相对变化量 (-2048 ~ +2047)
    int32_t delta = (int32_t)adc_val - 2048;

    // 2. 映射到屏幕高度：2048 对应约 100 像素振幅 (乘以缩放系数)
    // 注意：因屏幕 Y 轴向下递增，电压增大时 Y 坐标应减小
    int32_t y = WAVE_CENTER_Y - (int32_t)(delta * g_v_scale * 100 / 2048) + g_y_offset;

    // 3. 边界硬截断，防止波形溢出侵入顶栏表头 (Y < 25) 或超出屏底
    if (y < WAVE_AREA_TOP)    y = WAVE_AREA_TOP;
    if (y > WAVE_AREA_BOTTOM) y = WAVE_AREA_BOTTOM;

    return (uint8_t)y;
}

/**
 * @brief 底层单点绘制封装
 */
void screen_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    st7789_draw_point(x, y, color);
}

/**
 * @brief 判断坐标是否落在参考网格线上，返回对应的底色
 */
static inline uint16_t get_grid_color(uint16_t x, uint16_t y)
{
    // 中心十字参考虚线 (Y = 132, X = 160)
    if ((y == WAVE_CENTER_Y && (x % 4 == 0)) || (x == 160 && (y % 4 == 0))) {
        return ST7789_COLOR_WHITE;
    }
    // 水平辅助参考线 (每隔 30 像素一条蓝色虚线)
    if ((y >= 42 && y <= 222) && ((y - 42) % 30 == 0) && (x % 16 == 0)) {
        return ST7789_COLOR_BLUE;
    }
    return ST7789_COLOR_BLACK; // 普通背景黑色
}

/**
 * @brief 绘制垂直实线段（用于连接相邻两列跳变过大的波形点）
 */
static void draw_vertical_line(uint16_t x, uint8_t y1, uint8_t y2, uint16_t color)
{
    uint8_t start = (y1 < y2) ? y1 : y2;
    uint8_t end   = (y1 < y2) ? y2 : y1;
    for (uint8_t y = start; y <= end; y++) {
        screen_draw_point(x, y, color);
    }
}

/**
 * @brief 垂直线段差分清除（清除上一帧连线并恢复被覆盖的网格像素）
 */
static void clear_vertical_line(uint16_t x, uint8_t y1, uint8_t y2)
{
    uint8_t start = (y1 < y2) ? y1 : y2;
    uint8_t end   = (y1 < y2) ? y2 : y1;
    for (uint8_t y = start; y <= end; y++) {
        screen_draw_point(x, y, get_grid_color(x, y));
    }
}

/**
 * @brief 差分擦除：仅清除上一帧绘制的点和跳变沿连线，并自动还原网格
 */
void screen_clear_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len)
{
    // 未初始化的旧帧缓存（如切页刚清零时）不进行擦除，防止破坏顶栏
    if (last_frame[0] < WAVE_AREA_TOP) return;

    // 1. 擦除第 0 列起始点
    if (last_frame[0] != draw_frame[0]) {
        screen_draw_point(0, last_frame[0], get_grid_color(0, last_frame[0]));
    }

    // 2. 擦除后续各列
    for (uint16_t i = 1; i < len; i++) {
        int16_t last_diff = (int16_t)last_frame[i] - (int16_t)last_frame[i - 1];

        // 若上一帧在此处绘制了垂直跳变沿，整条线段擦除并还原背景
        if (last_diff > 1 || last_diff < -1) {
            clear_vertical_line(i, last_frame[i - 1], last_frame[i]);
        } 
        else {
            // 普通水平/缓变点：仅当坐标改变时擦除，节省 SPI 开销
            if (last_frame[i] != draw_frame[i]) {
                screen_draw_point(i, last_frame[i], get_grid_color(i, last_frame[i]));
            }
        }
    }
}

/**
 * @brief 30 FPS 实时波形渲染主函数
 * @param last_frame 上一帧屏幕 Y 坐标数组 (长度 320)
 * @param draw_frame 当前待绘制的屏幕 Y 坐标数组 (长度 320)
 * @param len 绘制点数 (固定为 320)
 */
void screen_draw_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len)
{
    // 页面安全锁：若当前不在波形页，立即退出，防止跨页破坏画面
    if (current_page != PAGE_WAVE) return;

    // 1. 擦除上一帧波形与跳变线
    screen_clear_frame(last_frame, draw_frame, len);

    // 2. 绘制第 0 列起始点
    screen_draw_point(0, draw_frame[0], ST7789_COLOR_YELLOW);
    last_frame[0] = draw_frame[0];

    // 3. 绘制当前帧的新波形（相邻点差值 > 1 像素时自动补齐垂直线）
    for (uint16_t i = 1; i < len; i++) {
        int16_t diff = (int16_t)draw_frame[i] - (int16_t)draw_frame[i - 1];

        if (diff > 1 || diff < -1) {
            // 沿陡峭边缘垂直拉线，形成完整方波
            draw_vertical_line(i, draw_frame[i - 1], draw_frame[i], ST7789_COLOR_YELLOW);
        } else {
            screen_draw_point(i, draw_frame[i], ST7789_COLOR_YELLOW);
        }

        // 缓存当前点作为下一帧擦除的依据
        last_frame[i] = draw_frame[i];
    }
}
/**
 * @brief 刷新整个控制台内容区域
 */
static void console_refresh_all(void)
{
    for (uint8_t i = 0; i < CONSOLE_MAX_LINES; i++)
    {
        uint16_t y = CONSOLE_START_Y + i * CONSOLE_FONT_H;
        // 先用空格覆盖清空当前行旧内容，防止短字符串无法完全覆盖长字符串
        char blank_line[CONSOLE_MAX_COLS + 1];
        memset(blank_line, ' ', CONSOLE_MAX_COLS);
        blank_line[CONSOLE_MAX_COLS] = '\0';
        st7789_write_string(0, y, blank_line, CONSOLE_COLOR_BG, CONSOLE_FONT);

        // 如果该行有内容则写入
        if (console_buffer[i][0] != '\0')
        {
            st7789_write_string(0, y, console_buffer[i], CONSOLE_COLOR_TXT, CONSOLE_FONT);
        }
    }
}

/**
 * @brief 清空控制台日志
 */
void console_clear(void)
{
    memset(console_buffer, 0, sizeof(console_buffer));
    current_line = 0;
    console_refresh_all();
}

/**
 * @brief 向上滚动一行
 */
static void console_scroll_up(void)
{
    
    // 所有行依次向上搬移一行
    for (uint8_t i = 0; i < CONSOLE_MAX_LINES - 1; i++)
    {
        strncpy(console_buffer[i], console_buffer[i + 1], CONSOLE_MAX_COLS);
        console_buffer[i][CONSOLE_MAX_COLS] = '\0';
    }
    // 清空最后一行
    memset(console_buffer[CONSOLE_MAX_LINES - 1], 0, sizeof(console_buffer[0]));
}

/**
 * @brief 输出一行文本到控制台（自动换行与滚屏）
 */
void console_print(const char *str)
{
    if (str == NULL) return;

    // 1. 无论在哪个页面，RAM 缓冲区照常更新与滚屏
    if (current_line >= CONSOLE_MAX_LINES)
    {
        console_scroll_up();
        current_line = CONSOLE_MAX_LINES - 1;
    }

    strncpy(console_buffer[current_line], str, CONSOLE_MAX_COLS);
    console_buffer[current_line][CONSOLE_MAX_COLS] = '\0';
    current_line++;

    // 2. 核心保护：只有当前处于控制台页面时，才真正操作 LCD 刷屏
    if (current_page == PAGE_CONSOLE)
    {
        console_refresh_all();
    }
}

/**
 * @brief 格式化输出函数（类似 printf）
 */
void console_printf(const char *fmt, ...)
{
    char log_buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_buf, sizeof(log_buf), fmt, args);
    va_end(args);

    console_print(log_buf);
}

/**
 * @brief 进入控制台页面初始化UI
 */
void page_console(void)
{
    current_page = PAGE_CONSOLE;

    // 1. 清空顶栏并重绘标题与分割线
    st7789_fill_rect(0, 0, 319, 24, ST7789_COLOR_BLACK);
    st7789_write_string(1, 1, "CONSOLE", ST7789_COLOR_GREEN, ST7789_FONT_24);
    for (int i = 0; i < 320; i++)
    {
        st7789_draw_point(i, 24, ST7789_COLOR_GREEN);
    }

    // 2. 关键修复：彻底擦除上一页在主体内容区域（Y: 25 ~ 239）残留的所有图像
    st7789_fill_rect(0, 25, 319, 239, ST7789_COLOR_BLACK);

    // 3. 重新绘制控制台 RAM 缓冲区里的文本
    console_refresh_all();
}
/* ==================================================================== */
/*                            波形页实现 (PAGE_WAVE)                     */
/* ==================================================================== */

/**
 * @brief 绘制示波器中心十字参考虚线与边框刻度
 */
void wave_draw_grid(void)
{
    // 中心横轴 (Y = 132，即示波区 25~239 的中间)
    uint16_t center_y = 132;
    for (uint16_t x = 0; x < 320; x += 4) {
        st7789_draw_point(x, center_y, ST7789_COLOR_WHITE);
    }

    // 中心纵轴 (X = 160)
    for (uint16_t y = UI_BODY_START_Y; y <= UI_BODY_END_Y; y += 4) {
        st7789_draw_point(160, y, ST7789_COLOR_WHITE);
    }

    // 水平网格刻度 (每隔 30 像素打点标定电压分度)
    for (uint16_t y = UI_BODY_START_Y + 17; y < UI_BODY_END_Y; y += 30) {
        if (y == center_y) continue;
        for (uint16_t x = 0; x < 320; x += 16) {
            st7789_draw_point(x, y, ST7789_COLOR_BLUE);
        }
    }
}

/**
 * @brief 进入波形页面初始化
 */
void page_wave(void)
{
    current_page = PAGE_WAVE;

    // 1. 刷新顶栏 (0~24)
    st7789_fill_rect(0, 0, 319, 24, ST7789_COLOR_BLACK);
    st7789_write_string(1, 1, "WAVE", ST7789_COLOR_GREEN, ST7789_FONT_24);
    for (int i = 0; i < 320; i++) {
        st7789_draw_point(i, 24, ST7789_COLOR_GREEN);
    }

    // 2. 彻底抹除旧页面残留的主体区域 (25~239)，并画上示波器十字网格
    st7789_fill_rect(0, 25, 319, 239, ST7789_COLOR_BLACK);
    wave_draw_grid();
}
/* ==================================================================== */
/*                            参数页实现 (PAGE_PARAM)                    */
/* ==================================================================== */

/**
 * @brief 绘制/刷新所有测量参数项
 */
void param_refresh_all(void)
{
    char val_str[32];

    // 第一行：频率 (Freq) 与 占空比 (Duty)
    st7789_write_string(10, 40, "Freq:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    if (current_params.freq >= 1000.0f) {
        snprintf(val_str, sizeof(val_str), "%6.2f kHz", current_params.freq / 1000.0f);
    } else {
        snprintf(val_str, sizeof(val_str), "%6.1f Hz ", current_params.freq);
    }
    st7789_write_string(60, 40, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);

    st7789_write_string(170, 40, "Duty:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    snprintf(val_str, sizeof(val_str), "%5.1f %%", current_params.duty);
    st7789_write_string(220, 40, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);

    // 第二行：峰峰值 (Vpp) 与 有效值 (Vrms)
    st7789_write_string(10, 80, "Vpp :", ST7789_COLOR_GREEN, ST7789_FONT_16);
    snprintf(val_str, sizeof(val_str), "%6.2f V ", current_params.vpp);
    st7789_write_string(60, 80, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);

    st7789_write_string(170, 80, "Vrms:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    snprintf(val_str, sizeof(val_str), "%5.2f V", current_params.vrms);
    st7789_write_string(220, 80, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);

    // 第三行：最大电压 (Vmax) 与 最小电压 (Vmin)
    st7789_write_string(10, 120, "Vmax:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    snprintf(val_str, sizeof(val_str), "%6.2f V ", current_params.vmax);
    st7789_write_string(60, 120, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);

    st7789_write_string(170, 120, "Vmin:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    snprintf(val_str, sizeof(val_str), "%5.2f V", current_params.vmin);
    st7789_write_string(220, 120, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);

    // 第四行：系统采集状态装饰栏
    st7789_write_string(10, 170, "CH1 Coupling: DC", ST7789_COLOR_WHITE, ST7789_FONT_16);
    st7789_write_string(10, 195, "ADC Sampling: DMA Circular", ST7789_COLOR_WHITE, ST7789_FONT_16);
}

/**
 * @brief 更新测量参数数据（带页面状态保护）
 */
void param_set_data(const MeasureParams_t *params)
{
    if (params == NULL) return;

    // 1. 数据更新进 RAM 结构体
    memcpy(&current_params, params, sizeof(MeasureParams_t));

    // 2. 只有当前处于参数页时才刷新屏幕，防止破坏其他页面画面
    if (current_page == PAGE_PARAM) {
        param_refresh_all();
    }
}

/**
 * @brief 进入参数页面初始化
 */
void page_param()
{
    current_page = PAGE_PARAM;

    // 1. 绘制顶栏标题与分割线
    st7789_fill_rect(0, 0, 319, UI_HEADER_H - 1, ST7789_COLOR_BLACK);
    st7789_write_string(1, 1, "PARAM", ST7789_COLOR_GREEN, ST7789_FONT_24);

    for (int i = 0; i < 320; i++) {
        st7789_draw_point(i, UI_HEADER_H, ST7789_COLOR_GREEN);
    }

    // 2. 清空主体区域并绘制当前的测量参数
    st7789_fill_rect(0, UI_BODY_START_Y, 319, UI_BODY_END_Y, ST7789_COLOR_BLACK);
    param_refresh_all();
}






void page_prev(PAGE* current_page) {
    

}

void screen_clear() {
    
}

void page_next(PAGE* current_page) {
    
}

void y_zoom_in(uint8_t y_scale) {
    

}

void x_zoom_in() {

}

void x_zoom_out() {

}

void x_shift_left() {

}

void x_shift_right() {

}

void reset_view() {

}

void run_stop_toggle() {

}


