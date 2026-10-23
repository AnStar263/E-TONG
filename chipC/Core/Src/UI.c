#include "UI.h"
#include "main.h"
void screen_draw_point(uint16_t x, uint16_t y, COLOR color) {

}

void screen_clear_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len) {
    for (int i = 1; i < len; i++) {
        if (last_frame[i] != draw_frame[i]) {
            screen_draw_point(i, last_frame[i], BLACK);
        }
    }
}

void screen_draw_frame(uint8_t* last_frame, uint8_t* draw_frame, uint16_t len) {
    if (!last_frame[0]) {
        draw_frame[0] = 1;
    } else {
        screen_clear_frame(last_frame, draw_frame, len);
    }    
    for (int i = 0; i < len; i++) {
        last_frame[i] = draw_frame[i];
        screen_draw_point(i, draw_frame[i], WHITE);
    }
}




void page_prev(PAGE* current_page) {
    if (*current_page == PAGE_PARAM) {
        *current_page = PAGE_FFT;
        return;
    }
    *current_page -= 1;
    screen_clear();

}

void screen_clear() {
    
}

void page_next(PAGE* current_page) {
    if (*current_page == PAGE_FFT) {
        *current_page = PAGE_PARAM;
        return;
    }
    *current_page += 1;
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


