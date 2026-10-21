#ifndef __FFT_MATH_
#define __FFT_MATH_

#define PI 3.14159265358979323846f

typedef struct {
    float real;
    float imag;
} Complex;

// 测频与幅值校正结果结构体
typedef struct {
    float freq;     // 校正后的频率 (Hz)
    float amp;      // 校正后的幅值 (Peak)
    int   peak_bin; // 原始最大谱线索引
    float delta;    // 频偏系数
} RifeResult;

static inline Complex complex_mul(Complex a, Complex b);
void bit_reverse(Complex *x, int N);
void fft(Complex *x, int N);
void compute_raw_magnitude(const Complex *x, float *mag, int N);
RifeResult rife_estimate(const float *mag, int N, float fs);





#endif
