#ifndef __FFT_MATH_H
#define __FFT_MATH_H

#include <stdint.h>

#define FS_INIT     2100000.0f
#define SAMPLE_N    1024

void measure_signal(const uint16_t *adc_buf, uint16_t len, float fs);

#endif