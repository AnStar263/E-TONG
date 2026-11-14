#include "UI.h"
#include "main.h"
#include "driver_st7789.h"
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

/* --- 控制台参数配置 --- */
#define CONSOLE_FONT          ST7789_FONT_16
#define CONSOLE_FONT_W        8
#define CONSOLE_FONT_H        16
#define CONSOLE_START_Y       28
#define CONSOLE_COLOR_BG      ST7789_COLOR_BLACK
#define CONSOLE_COLOR_TXT     ST7789_COLOR_WHITE

#define CONSOLE_MAX_COLS      (320 / CONSOLE_FONT_W)
#define CONSOLE_MAX_LINES     ((240 - CONSOLE_START_Y) / CONSOLE_FONT_H)

/* --- 基础界面宏定义 (320x240) --- */
#define UI_HEADER_H           24
#define UI_BODY_START_Y       25
#define UI_BODY_END_Y         239

// 波形页垂直显示范围
#define WAVE_AREA_TOP         25
#define WAVE_AREA_BOTTOM      239
#define WAVE_CENTER_Y         132

// 参数页底部简易波形显示范围 (Y: 146 ~ 238, 中心 Y = 192)
#define PARAM_WAVE_DIV_Y      144
#define PARAM_WAVE_TOP        146
#define PARAM_WAVE_BOTTOM     238
#define PARAM_WAVE_CENTER_Y   192

volatile uint16_t g_v_scale = 1;
static int16_t g_y_offset = 0;

#define TB_TABLE_SIZE         11
#define TB_MID_INDEX          6

volatile int16_t g_x_offset = 0;
#define X_SHIFT_STEP          16
#define X_SHIFT_MAX           320

volatile MeasureParams_t current_params = {0};
volatile PAGE current_page = PAGE_CONSOLE;

static char console_buffer[CONSOLE_MAX_LINES][CONSOLE_MAX_COLS + 1];
static uint8_t current_line = 0;

static MeasureParams_t last_params = {
    .freq = -1.0f,
    .vavg = -999.0f,
    .vpp  = -1.0f,
    .vmax = -999.0f,
    .vmin = -999.0f
};

static const TimeBase_t g_tb_table[TB_TABLE_SIZE] = {
    {   1.0e-6f, "  1us" }, 
    {   2.0e-6f, "  2us" }, 
    {   5.0e-6f, "  5us" },
    {  10.0e-6f, " 10us" }, 
    {  20.0e-6f, " 20us" }, 
    {  50.0e-6f, " 50us" },
    { 100.0e-6f, "100us" }, 
    { 200.0e-6f, "200us" }, 
    { 500.0e-6f, "500us" },
    {   1.0e-3f, "  1ms" }, 
    {   2.0e-3f, "  2ms" }
};

// 对应 11 档时基的屏幕周期倍数（中心第 3 档严格对应 1.0 单周期）
static const float g_cycle_mult[TB_TABLE_SIZE] = {
    0.20f, 0.30f, 0.40f, 0.50f, 0.75f, 0.90f,
    1.00f, 1.50f, 2.00f, 2.50f, 5.00f
};

volatile uint8_t s_current_tb_idx = TB_MID_INDEX;
static uint8_t s_auto_tb_enable = 1;

void ui_update_topbar_volt_div(void)
{
    char buf[16];
    float v_div = 1.5f / (float)g_v_scale;

    if (v_div >= 1.0f) {
        snprintf(buf, sizeof(buf), "VB:%.1fV", v_div);
    } else {
        snprintf(buf, sizeof(buf), "VB:%dmV", (int)(v_div * 1000.0f + 0.5f));
    }

    st7789_fill_rect(175, 1, 260, 23, ST7789_COLOR_BLACK);
    st7789_write_string(175, 4, buf, ST7789_COLOR_YELLOW, ST7789_FONT_16);
}

/**
 * @brief 单周期锁定与手动缩放步进换算
 */
uint8_t ui_calc_auto_timebase(float freq, float fs, float *out_step)
{
    if (s_auto_tb_enable) {
        s_current_tb_idx = TB_MID_INDEX; // 自适应模式下锁定 1.0 单周期档位
    }

    if (freq > 10.0f) {
        // 单周期采样点数 = fs / freq，映射到 320 像素宽度并乘以当前缩放系数
        float period_samples = fs / freq;
        float step = (period_samples * g_cycle_mult[s_current_tb_idx]) / 320.0f;
        if (step < 0.02f) step = 0.02f;
        if (step > 4.5f)  step = 4.5f;
        *out_step = step;
    } else {
        *out_step = 1.0f;
    }

    // 计算当前对应的等效每格时基索引用于顶栏显示
    if (s_auto_tb_enable && freq > 10.0f) {
        float target_div = (32.0f * (*out_step)) / fs; // 整屏320点(10格)对应的每格时间
        int best_idx = 0;
        float min_err = 1e9f;
        for (int i = 0; i < TB_TABLE_SIZE; i++) {
            float err = fabsf(g_tb_table[i].time_div - target_div);
            if (err < min_err) {
                min_err = err;
                best_idx = i;
            }
        }
        return (uint8_t)best_idx;
    }

    return s_current_tb_idx;
}

void ui_update_topbar_timebase(uint8_t tb_idx, uint8_t force)
{
    static uint8_t s_last_rendered_tb = 0xFF;
    if (tb_idx == s_last_rendered_tb && !force) return;
    s_last_rendered_tb = tb_idx;

    char buf[16];
    snprintf(buf, sizeof(buf), "TB:%s", g_tb_table[tb_idx].label);

    st7789_fill_rect(90, 1, 175, 23, ST7789_COLOR_BLACK);
    st7789_write_string(90, 4, buf, ST7789_COLOR_YELLOW, ST7789_FONT_16);
}

uint8_t wave_adc_to_screen_y(uint16_t adc_val)
{
    int32_t delta = (int32_t)adc_val - 2048;
    int32_t y = WAVE_CENTER_Y - (int32_t)(delta * g_v_scale * 100 / 2048) + g_y_offset;

    if (y < WAVE_AREA_TOP)    y = WAVE_AREA_TOP;
    if (y > WAVE_AREA_BOTTOM) y = WAVE_AREA_BOTTOM;

    return (uint8_t)y;
}

/**
 * @brief 将 ADC 值映射为参数页底部简易波形 Y 坐标 (146 ~ 238)
 */
uint8_t wave_adc_to_param_y(uint16_t adc_val)
{
    int32_t delta = (int32_t)adc_val - 2048;
    int32_t y = PARAM_WAVE_CENTER_Y - (int32_t)(delta * g_v_scale * 42 / 2048);

    if (y < PARAM_WAVE_TOP)    y = PARAM_WAVE_TOP;
    if (y > PARAM_WAVE_BOTTOM) y = PARAM_WAVE_BOTTOM;

    return (uint8_t)y;
}

void screen_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    st7789_draw_point(x, y, color);
}

static inline uint16_t get_grid_color(uint16_t x, uint16_t y)
{
    if (y == WAVE_CENTER_Y && (x % 4 == 0)) {
        return ST7789_COLOR_WHITE;
    }
    if (x == 160 && (y >= 30 && y <= 234) && (y % 6 == 0)) {
        return ST7789_COLOR_WHITE;
    }
    if ((y >= 42 && y <= 222) && ((y - 42) % 30 == 0) && (x % 16 == 0)) {
        return ST7789_COLOR_BLUE;
    }
    return ST7789_COLOR_BLACK;
}

static inline uint16_t get_param_grid_color(uint16_t x, uint16_t y)
{
    if (y == PARAM_WAVE_CENTER_Y && (x % 4 == 0)) {
        return ST7789_COLOR_WHITE;
    }
    if (x == 160 && (y >= PARAM_WAVE_TOP && y <= PARAM_WAVE_BOTTOM) && (y % 6 == 0)) {
        return ST7789_COLOR_WHITE;
    }
    return ST7789_COLOR_BLACK;
}

static void draw_vertical_line(uint16_t x, uint8_t y1, uint8_t y2, uint16_t color)
{
    uint8_t start = (y1 < y2) ? y1 : y2;
    uint8_t end   = (y1 < y2) ? y2 : y1;
    st7789_fill_rect(x, start, x, end, color);
}

static void clear_vertical_line(uint16_t x, uint8_t y1, uint8_t y2)
{
    uint8_t start = (y1 < y2) ? y1 : y2;
    uint8_t end   = (y1 < y2) ? y2 : y1;
    for (uint8_t y = start; y <= end; y++) {
        screen_draw_point(x, y, get_grid_color(x, y));
    }
}

static void clear_param_vertical_line(uint16_t x, uint8_t y1, uint8_t y2)
{
    uint8_t start = (y1 < y2) ? y1 : y2;
    uint8_t end   = (y1 < y2) ? y2 : y1;
    for (uint8_t y = start; y <= end; y++) {
        screen_draw_point(x, y, get_param_grid_color(x, y));
    }
}

void screen_clear_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len)
{
    if (last_frame[0] < WAVE_AREA_TOP) return;

    if (last_frame[0] != draw_frame[0]) {
        screen_draw_point(0, last_frame[0], get_grid_color(0, last_frame[0]));
    }

    for (uint16_t i = 1; i < len; i++) {
        int16_t last_diff = (int16_t)last_frame[i] - (int16_t)last_frame[i - 1];
        if (last_diff > 1 || last_diff < -1) {
            clear_vertical_line(i, last_frame[i - 1], last_frame[i]);
        } else {
            if (last_frame[i] != draw_frame[i]) {
                screen_draw_point(i, last_frame[i], get_grid_color(i, last_frame[i]));
            }
        }
    }
}

void screen_draw_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len)
{
    if (current_page != PAGE_WAVE) return;

    for (uint16_t i = 0; i < len; i++) {
        if (last_frame[i] >= WAVE_AREA_TOP) {
            if (i == 0 || last_frame[i - 1] < WAVE_AREA_TOP) {
                screen_draw_point(i, last_frame[i], get_grid_color(i, last_frame[i]));
            } else {
                int16_t ldiff = (int16_t)last_frame[i] - (int16_t)last_frame[i - 1];
                if (ldiff > 1 || ldiff < -1)
                    clear_vertical_line(i, last_frame[i - 1], last_frame[i]);
                else if (last_frame[i] != draw_frame[i])
                    screen_draw_point(i, last_frame[i], get_grid_color(i, last_frame[i]));
            }
        }

        if (draw_frame[i] >= WAVE_AREA_TOP) {
            if (i == 0 || draw_frame[i - 1] < WAVE_AREA_TOP) {
                screen_draw_point(i, draw_frame[i], ST7789_COLOR_YELLOW);
            } else {
                int16_t diff = (int16_t)draw_frame[i] - (int16_t)draw_frame[i - 1];
                if (diff > 1 || diff < -1)
                    draw_vertical_line(i, draw_frame[i - 1], draw_frame[i], ST7789_COLOR_YELLOW);
                else
                    screen_draw_point(i, draw_frame[i], ST7789_COLOR_YELLOW);
            }
        }
    }
    memcpy(last_frame, draw_frame, len);
}
/**
 * @brief 参数页底部简易波形差分渲染
 */
void param_draw_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len)
{
    if (current_page != PAGE_PARAM) return;

    if (last_frame[0] >= PARAM_WAVE_TOP) {
        if (last_frame[0] != draw_frame[0]) {
            screen_draw_point(0, last_frame[0], get_param_grid_color(0, last_frame[0]));
        }
        for (uint16_t i = 1; i < len; i++) {
            int16_t last_diff = (int16_t)last_frame[i] - (int16_t)last_frame[i - 1];
            if (last_diff > 1 || last_diff < -1) {
                clear_param_vertical_line(i, last_frame[i - 1], last_frame[i]);
            } else if (last_frame[i] != draw_frame[i]) {
                screen_draw_point(i, last_frame[i], get_param_grid_color(i, last_frame[i]));
            }
        }
    }

    screen_draw_point(0, draw_frame[0], ST7789_COLOR_YELLOW);
    last_frame[0] = draw_frame[0];

    for (uint16_t i = 1; i < len; i++) {
        int16_t diff = (int16_t)draw_frame[i] - (int16_t)draw_frame[i - 1];
        if (diff > 1 || diff < -1) {
            draw_vertical_line(i, draw_frame[i - 1], draw_frame[i], ST7789_COLOR_YELLOW);
        } else {
            screen_draw_point(i, draw_frame[i], ST7789_COLOR_YELLOW);
        }
        last_frame[i] = draw_frame[i];
    }
}

static void console_refresh_all(void)
{
    st7789_fill_rect(0, CONSOLE_START_Y, 319, 239, CONSOLE_COLOR_BG); // 整块清屏替代逐个画空格
    for (uint8_t i = 0; i < CONSOLE_MAX_LINES; i++)
    {
        if (console_buffer[i][0] != '\0')
        {
            uint16_t y = CONSOLE_START_Y + i * CONSOLE_FONT_H;
            st7789_write_string(0, y, console_buffer[i], CONSOLE_COLOR_TXT, CONSOLE_FONT);
        }
    }
}
void console_clear(void)
{
    memset(console_buffer, 0, sizeof(console_buffer));
    current_line = 0;
    console_refresh_all();
}

static void console_scroll_up(void)
{
    for (uint8_t i = 0; i < CONSOLE_MAX_LINES - 1; i++)
    {
        strncpy(console_buffer[i], console_buffer[i + 1], CONSOLE_MAX_COLS);
        console_buffer[i][CONSOLE_MAX_COLS] = '\0';
    }
    memset(console_buffer[CONSOLE_MAX_LINES - 1], 0, sizeof(console_buffer[0]));
}

void console_print(const char *str)
{
    if (str == NULL) return;

    if (current_line >= CONSOLE_MAX_LINES)
    {
        console_scroll_up();
        current_line = CONSOLE_MAX_LINES - 1;
    }

    strncpy(console_buffer[current_line], str, CONSOLE_MAX_COLS);
    console_buffer[current_line][CONSOLE_MAX_COLS] = '\0';
    current_line++;

    if (current_page == PAGE_CONSOLE)
    {
        console_refresh_all();
    }
}

void console_printf(const char *fmt, ...)
{
    char log_buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_buf, sizeof(log_buf), fmt, args);
    va_end(args);

    console_print(log_buf);
}

void page_console(void)
{
    current_page = PAGE_CONSOLE;

    st7789_fill_rect(0, 0, 319, 24, ST7789_COLOR_BLACK);
    st7789_write_string(1, 1, "CONSOLE", ST7789_COLOR_GREEN, ST7789_FONT_24);
    for (int i = 0; i < 320; i++)
    {
        st7789_draw_point(i, 24, ST7789_COLOR_GREEN);
    }

    st7789_fill_rect(0, 25, 319, 239, ST7789_COLOR_BLACK);
    console_refresh_all();
}

void wave_draw_grid(void)
{
    uint16_t center_y = WAVE_CENTER_Y;

    for (uint16_t x = 0; x < 320; x += 4) {
        st7789_draw_point(x, center_y, ST7789_COLOR_WHITE);
    }

    for (uint16_t y = 30; y <= 234; y += 6) {
        st7789_draw_point(160, y, ST7789_COLOR_WHITE);
    }

    for (uint16_t y = 42; y <= 222; y += 30) {
        if (y == center_y) continue;
        for (uint16_t x = 0; x < 320; x += 16) {
            if (x == 160) continue;
            st7789_draw_point(x, y, ST7789_COLOR_BLUE);
        }
    }
}

void page_wave(void)
{
    current_page = PAGE_WAVE;

    st7789_fill_rect(0, 0, 319, 24, ST7789_COLOR_BLACK);
    st7789_write_string(1, 1, "WAVE", ST7789_COLOR_GREEN, ST7789_FONT_24);

    ui_update_topbar_timebase(s_current_tb_idx, 1);
    ui_update_topbar_volt_div();

    char strtemp[4] = {'\0'};
    snprintf(strtemp, sizeof(strtemp), "%dX", g_v_scale);
    st7789_write_string(280, 1, strtemp, ST7789_COLOR_RED, ST7789_FONT_24);

    for (int i = 0; i < 320; i++) {
        st7789_draw_point(i, 24, ST7789_COLOR_GREEN);
    }

    st7789_fill_rect(0, 25, 319, 239, ST7789_COLOR_BLACK);
    wave_draw_grid();
}

void param_refresh_all(void)
{
    char val_str[32];

    if (fabsf(current_params.freq - last_params.freq) >= 0.2f) {
        last_params.freq = current_params.freq;
        st7789_fill_rect(60, 40, 160, 56, ST7789_COLOR_BLACK);
        if (current_params.freq >= 1000.0f) {
            snprintf(val_str, sizeof(val_str), "%6.2f kHz", current_params.freq / 1000.0f);
        } else {
            snprintf(val_str, sizeof(val_str), "%6.1f Hz ", current_params.freq);
        }
        st7789_write_string(60, 40, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);
    }

    if (fabsf(current_params.vavg - last_params.vavg) >= 0.01f) {
        last_params.vavg = current_params.vavg;
        st7789_fill_rect(220, 40, 310, 56, ST7789_COLOR_BLACK);
        snprintf(val_str, sizeof(val_str), "%5.2f V", current_params.vavg);
        st7789_write_string(220, 40, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);
    }

    if (fabsf(current_params.vpp - last_params.vpp) >= 0.01f) {
        last_params.vpp = current_params.vpp;
        st7789_fill_rect(60, 80, 160, 96, ST7789_COLOR_BLACK);
        snprintf(val_str, sizeof(val_str), "%6.2f V ", current_params.vpp);
        st7789_write_string(60, 80, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);
    }

    if (fabsf(current_params.vmax - last_params.vmax) >= 0.01f) {
        last_params.vmax = current_params.vmax;
        st7789_fill_rect(60, 115, 160, 131, ST7789_COLOR_BLACK);
        snprintf(val_str, sizeof(val_str), "%6.2f V ", current_params.vmax);
        st7789_write_string(60, 115, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);
    }

    if (fabsf(current_params.vmin - last_params.vmin) >= 0.01f) {
        last_params.vmin = current_params.vmin;
        st7789_fill_rect(220, 115, 310, 131, ST7789_COLOR_BLACK);
        snprintf(val_str, sizeof(val_str), "%5.2f V", current_params.vmin);
        st7789_write_string(220, 115, val_str, ST7789_COLOR_WHITE, ST7789_FONT_16);
    }
}

void param_set_data(const MeasureParams_t *params)
{
    if (params == NULL) return;
    memcpy((void *)&current_params, params, sizeof(MeasureParams_t));
    if (current_page == PAGE_PARAM) {
        param_refresh_all();
    }
}

void page_param(void)
{
    current_page = PAGE_PARAM;

    st7789_fill_rect(0, 0, 319, UI_HEADER_H - 1, ST7789_COLOR_BLACK);
    st7789_write_string(1, 1, "PARAM", ST7789_COLOR_GREEN, ST7789_FONT_24);
    for (int i = 0; i < 320; i++) st7789_draw_point(i, UI_HEADER_H, ST7789_COLOR_GREEN);
    st7789_fill_rect(0, UI_BODY_START_Y, 319, UI_BODY_END_Y, ST7789_COLOR_BLACK);

    // 上半部分：5 个实时测量参数
    st7789_write_string(10, 40,  "Freq:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    st7789_write_string(170, 40, "Vavg:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    st7789_write_string(10, 80,  "Vpp :", ST7789_COLOR_GREEN, ST7789_FONT_16);
    st7789_write_string(10, 115, "Vmax:", ST7789_COLOR_GREEN, ST7789_FONT_16);
    st7789_write_string(170, 115,"Vmin:", ST7789_COLOR_GREEN, ST7789_FONT_16);

    // 下半部分：替换原两行文字为简易波形区 (Y: 144 ~ 239)
    for (uint16_t x = 0; x < 320; x++) {
        st7789_draw_point(x, PARAM_WAVE_DIV_Y, ST7789_COLOR_GREEN);
    }
    for (uint16_t x = 0; x < 320; x += 4) {
        st7789_draw_point(x, PARAM_WAVE_CENTER_Y, ST7789_COLOR_WHITE);
    }
    for (uint16_t y = PARAM_WAVE_TOP; y <= PARAM_WAVE_BOTTOM; y += 6) {
        st7789_draw_point(160, y, ST7789_COLOR_WHITE);
    }

    last_params.freq = -1.0f;
    last_params.vavg = -999.0f;
    last_params.vpp  = -1.0f;
    last_params.vmax = -999.0f;
    last_params.vmin = -999.0f;

    param_refresh_all();
}

void page_prev(PAGE* current_page) {}
void screen_clear(void) {}
void page_next(PAGE* current_page) {}

void set_y_scale(uint8_t y_scale) {
    g_v_scale = y_scale;
}

void x_zoom_in(void)
{
    if (s_current_tb_idx > 0) {
        s_current_tb_idx--;
        s_auto_tb_enable = 0;
    }
}

void x_zoom_out(void)
{
    if (s_current_tb_idx < TB_TABLE_SIZE - 1) {
        s_current_tb_idx++;
        s_auto_tb_enable = 0;
    }
}

void reset_view(void)
{
    g_v_scale = 1;
    s_current_tb_idx = TB_MID_INDEX;
    s_auto_tb_enable = 1;
    g_x_offset = 0;
}

void x_shift_left(void)
{
    if (g_x_offset < X_SHIFT_MAX) {
        g_x_offset += X_SHIFT_STEP;
    }
}

void x_shift_right(void)
{
    if (g_x_offset > -X_SHIFT_MAX) {
        g_x_offset -= X_SHIFT_STEP;
    }
}

void run_stop_toggle(void) {}