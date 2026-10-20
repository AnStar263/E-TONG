#ifndef __UI_H
#define __UI_H

#include "main.h"

typedef enum {
    PAGE_PARAM,
    PAGE_WAVE,
    PAGE_FFT
} PAGE;

void screen_draw_point(uint16_t x, uint16_t y);
void screen_draw_axis();
void screen_clear();


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
