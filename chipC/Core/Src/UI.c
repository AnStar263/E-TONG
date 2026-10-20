#include "UI.h"

void page_prev(PAGE* current_page) {
    if (*current_page == PAGE_PARAM) {
        *current_page = PAGE_FFT;
        return;
    }
    *current_page -= 1;
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




