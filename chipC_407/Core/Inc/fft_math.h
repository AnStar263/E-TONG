#ifndef __FFT_MATH_H
#define __FFT_MATH_H

#include <stdint.h>

#define FS_COARSE     1000000.0f   // 保证 AD9226 时钟不低于 1 MHz
#define FS_LOW_FINE   1000000.0f
#define SAMPLE_N    1024

void measure_signal(const uint16_t *adc_buf, uint16_t len, float fs);

#endif