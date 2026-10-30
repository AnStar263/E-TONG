#ifndef __UI_H
#define __UI_H

#include "main.h"
#include <stdint.h>
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

void page_console ();
// 控制台输出接口
void console_clear();
void console_print(const char *str);
void console_printf(const char *fmt, ...);


void page_wave();
void wave_draw_grid();


/* 参数测量数据结构体 */
typedef struct {
    float freq;         // 频率 (Hz)
    float vpp;          // 峰峰值 (V)
    float vmax;         // 最大电压 (V)
    float vmin;         // 最小电压 (V)
    float vrms;         // 有效值 (V)
    float duty;         // 占空比 (%)
} MeasureParams_t;
void page_param();
void param_set_data(const MeasureParams_t *params);
void param_refresh_all(void);

void page_prev(PAGE* current_page);
void page_next(PAGE* current_page);
void y_zoom_in(uint8_t y_scale);
void x_zoom_in();
void x_zoom_out();
void x_shift_left();
void x_shift_right();
void reset_view();
void run_stop_toggle();;

#endif
