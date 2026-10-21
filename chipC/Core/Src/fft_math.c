#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fft_math.h"



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
