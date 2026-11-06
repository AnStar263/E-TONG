#include "fft_math.h"
#include "UI.h"
#include <math.h>

#define MIN_LAG     2       // 对应最高理论测频 fs / 2 (约 1MHz)
#define MAX_LAG     512     // 对应最低测频约 4.1kHz (覆盖 1024 点窗口)

extern volatile MeasureParams_t current_params;

/**
 * @brief 信号参数一站式测量：频率(ACF)、Vavg、Vmax、Vmin、Vpp
 */
void measure_signal(const uint16_t *adc_buf, uint16_t len, float fs)
{
    // ================= 1. 平均电压 (Vavg) =================
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += adc_buf[i];
    }
    float avg_raw = (float)sum / (float)len;
    current_params.vavg = (avg_raw / 4096.0f) * 10.0f;

    // ================= 2. 5点中值滤波提取 Vmax, Vmin, Vpp =================
    uint16_t v_max_raw = 0;
    uint16_t v_min_raw = 4095;
    uint16_t win[5];

    for (int i = 0; i <= len - 5; i++) {
        for (int k = 0; k < 5; k++) win[k] = adc_buf[i + k];

        // 5 点快速冒泡排序
        for (int m = 0; m < 4; m++) {
            for (int n = 0; n < 4 - m; n++) {
                if (win[n] > win[n + 1]) {
                    uint16_t t = win[n]; win[n] = win[n + 1]; win[n + 1] = t;
                }
            }
        }
        uint16_t mid = win[2];
        if (mid > v_max_raw) v_max_raw = mid;
        if (mid < v_min_raw) v_min_raw = mid;
    }

    // 1. 平均电压：以 2048 为零点，范围 -5V ~ +5V
    current_params.vavg = ((avg_raw - 2048.0f) / 2048.0f) * 5.0f;

    // 2. 最大与最小电压
    current_params.vmax = (((float)v_max_raw - 2048.0f) / 2048.0f) * 5.0f;
    current_params.vmin = (((float)v_min_raw - 2048.0f) / 2048.0f) * 5.0f;

    // 3. 峰峰值（差值自然抵消直流偏置）
    current_params.vpp  = current_params.vmax - current_params.vmin;

    // ================= 3. 噪声门限检查 (纯直流/无信号判定) =================
    if ((v_max_raw - v_min_raw) < 20) { // 波动低于约 50mV 视为纯直流 0Hz
        current_params.freq = 0.0f;
        return;
    }

    // ================= 4. 去直流短时自相关 (ACF) 测频 =================
    static float x[SAMPLE_N];
    for (uint16_t i = 0; i < len; i++) {
        x[i] = (float)adc_buf[i] - avg_raw;
    }

    static float r[MAX_LAG + 2];
    int peak_lag = -1;
    uint8_t passed_valley = 0;

    // 动态搜索：先越过首个波谷，再抓取第一个相关峰
    for (int tau = MIN_LAG; tau < MAX_LAG; tau++) {
        float acc = 0.0f;
        int calc_len = len - tau;
        for (int n = 0; n < calc_len; n++) {
            acc += x[n] * x[n + tau];
        }
        r[tau] = acc / (float)calc_len;

        if (tau > MIN_LAG) {
            if (!passed_valley) {
                // 下降趋势停止转为上升，确认已越过零点主瓣下降区（到达波谷）
                if (r[tau] > r[tau - 1]) {
                    passed_valley = 1;
                }
            } else {
                // 上升趋势停止转为下降，确认已找到第一个周期峰
                if (r[tau - 1] > 0.0f && r[tau] < r[tau - 1]) {
                    peak_lag = tau - 1;
                    break; // 提前退出，单片机开销极低
                }
            }
        }
    }

    if (peak_lag <= 0) {
        current_params.freq = 0.0f;
        return;
    }

    // 三点抛物线插值求极值点精细偏移
    float y1 = r[peak_lag - 1];
    float y2 = r[peak_lag];
    float y3 = r[peak_lag + 1];
    float denom = 2.0f * (2.0f * y2 - y1 - y3);
    float delta = 0.0f;

    if (fabsf(denom) > 1e-6f) {
        delta = (y3 - y1) / denom;
    }

    float exact_period = (float)peak_lag + delta;
    if (exact_period > 1.0f) {
        current_params.freq = fs / exact_period;
    } else {
        current_params.freq = 0.0f;
    }
}