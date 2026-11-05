#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "fft_math.h"

#define FFT_N       1024
#define FS_INIT    2100000.0f
static Complex fft_in[FFT_N];
static float   fft_mag[FFT_N / 2]; 
volatile RifeResult res;
volatile uint16_t param_m_flag = 0;

// 复数乘法
static inline Complex complex_mul(Complex a, Complex b) {
    Complex c;
    c.real = a.real * b.real - a.imag * b.imag;
    c.imag = a.real * b.imag + a.imag * b.real;
    return c;
} 

// 1. 雷德算法进行倒位序重排
void bit_reverse(Complex *x, int N) {
    int j = 0;
    for (int i = 0; i < N - 1; i++) {
        if (i < j) {
            Complex temp = x[i];
            x[i] = x[j];
            x[j] = temp;
        }
        int k = N >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
}

// 2. 基-2 时间抽取 (DIT) FFT
void fft(Complex *x, int N) {
    bit_reverse(x, N);

    for (int step = 2; step <= N; step <<= 1) {
        int half_step = step >> 1;
        float angle = -2.0f * PI / (float)step;
        Complex wn = {cosf(angle), sinf(angle)};

        for (int i = 0; i < N; i += step) {
            Complex w = {1.0f, 0.0f};
            for (int j = 0; j < half_step; j++) {
                Complex u = x[i + j];
                Complex v = complex_mul(w, x[i + j + half_step]);

                x[i + j].real = u.real + v.real;
                x[i + j].imag = u.imag + v.imag;

                x[i + j + half_step].real = u.real - v.real;
                x[i + j + half_step].imag = u.imag - v.imag;

                w = complex_mul(w, wn);
            }
        }
    }
}

// 3. 计算单边未归一化模长谱（只算到 N/2）
void compute_raw_magnitude(const Complex *x, float *mag, int N) {
    for (int i = 0; i < N / 2; i++) {
        mag[i] = sqrtf(x[i].real * x[i].real + x[i].imag * x[i].imag);
    }
}

// 4. Rife 谱线插值算法核心函数
RifeResult rife_estimate(const float *mag, int N, float fs) {
    RifeResult res = {0};
    float df = fs / (float)N; // 频率分辨率

    // 寻找主瓣峰值谱线 k0（跳过 DC 直流分量 i=0，防止直流偏置干扰）
    int k0 = 1;
    float max_val = mag[1];
    for (int i = 2; i < (N / 2) - 1; i++) {
        if (mag[i] > max_val) {
            max_val = mag[i];
            k0 = i;
        }
    }
    res.peak_bin = k0;

    // 比较左右相邻谱线确定偏向
    int r0 = 0;
    float neighbor_val = 0.0f;
    if (mag[k0 + 1] >= mag[k0 - 1]) {
        r0 = 1;
        neighbor_val = mag[k0 + 1];
    } else {
        r0 = -1;
        neighbor_val = mag[k0 - 1];
    }

    // 计算比值 r 与频偏 delta
    float r = neighbor_val / max_val;
    float delta = (float)r0 * (r / (1.0f + r));
    res.delta = delta;

    // 校正频率计算
    res.freq = ((float)k0 + delta) * df;

    // 幅度修正系数: pi * delta / sin(pi * delta)
    float amp_correct = 1.0f;
    if (fabsf(delta) > 1e-5f) {
        float pi_delta = PI * delta;
        amp_correct = pi_delta / sinf(pi_delta);
    }

    // 实际单边峰值幅值 (交流分量乘 2 / N)
    res.amp = (2.0f * max_val / (float)N) * amp_correct;

    return res;
}

// 在状态机 Process 状态下调用：
void do_fft_process(uint16_t* adc_buf) {
    // 1. 填充复数数组（减去中位值 2048 消除直流，虚部赋 0）
    for (int i = 0; i < FFT_N; i++) {
        fft_in[i].real = (float)adc_buf[i] - 2048.0f;
        fft_in[i].imag = 0.0f;
    }

    // 2. 原位执行 FFT
    fft(fft_in, FFT_N);

    // 3. 计算前 512 点的幅度谱
    compute_raw_magnitude(fft_in, fft_mag, FFT_N);

    // 4. Rife 谱线插值估算
    res = rife_estimate(fft_mag, FFT_N, FS_INIT);
    param_m_flag = 1;
}

uint16_t cal_vpp(const uint16_t* adc_buf) {
    uint16_t v_max = 0;
    uint16_t v_min = 4095;
    uint16_t win[5];

    for (int i = 0; i <= FFT_N - 5; i++) {
        for (int k = 0; k < 5; k++) {
            win[k] = adc_buf[i + k];
        }

        for (int m = 0; m < 4; m++) {
            for (int n = 0; n < 4 - m; n++) {
                if (win[n] > win[n + 1]) {
                    uint16_t t = win[n];
                    win[n] = win[n + 1];
                    win[n + 1] = t;
                }
            }
        }
        
        uint16_t mid = win[2];
        v_max = v_max > mid ? v_max : mid;
        v_min = v_min < mid ? v_min : mid;
    }
    return v_max - v_min;
}
