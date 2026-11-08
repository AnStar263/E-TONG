#ifndef __UI_H
#define __UI_H

#include "main.h"
#include <stdint.h>
/* --- 自动时基相关声明 --- */
typedef struct {
    float time_div;     // 每格时间 (秒/格)
    const char *label;  // 顶栏显示文本 (如 "10us")
} TimeBase_t;

// 根据信号频率自适应计算最佳时基档位和屏幕像素抽样步进
uint8_t ui_calc_auto_timebase(float freq, float fs, float *out_step);

// 顶栏时基显示刷新（force=1 强制刷，force=0 仅在档位变动时局部刷新）
void ui_update_topbar_timebase(uint8_t tb_idx, uint8_t force);
typedef enum {
    PAGE_CONSOLE = 0,
    PAGE_WAVE,
    PAGE_PARAM
} PAGE;
typedef enum {
    BLACK = 0,
    WHITE
} COLOR;

void screen_draw_point(uint16_t x, uint16_t y, uint16_t color);
void screen_draw_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len);
void screen_clear();
uint8_t wave_adc_to_screen_y(uint16_t adc_val);
void page_console ();
// 控制台输出接口
void console_clear();
void console_print(const char *str);
void console_printf(const char *fmt, ...);


void page_wave();
void wave_draw_grid();
void ui_update_topbar_volt_div();

typedef struct {
    float freq;     // 频率 (Hz)
    float vavg;     // 平均电压 (V)
    float vpp;      // 峰峰值 (V)
    float vmax;     // 最大电压 (V)
    float vmin;     // 最小电压 (V)
} MeasureParams_t;
void page_param();
void param_set_data(const MeasureParams_t *params);
void param_refresh_all(void);

void page_prev(PAGE* current_page);
void page_next(PAGE* current_page);
void set_y_scale(uint8_t y_scale);
void screen_show_param (float freq, float v_avg, float v_max, float v_vpp);

void x_zoom_in();
void x_zoom_out();
void x_shift_left();
void x_shift_right();
void reset_view();
void run_stop_toggle();;

#endif
