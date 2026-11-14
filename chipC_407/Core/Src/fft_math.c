#include "fft_math.h"
#include "UI.h"
#include <math.h>

#define MIN_LAG     2
#define MAX_LAG     700     // 扩大搜索窗口以无缝衔接粗测与低频精测

extern volatile MeasureParams_t current_params;

/**
 * @brief 计算指定延迟 tau 的精确自相关值
 */
static float calc_acf_at(const float *x, uint16_t len, int tau)
{
    float acc = 0.0f;
    int calc_len = len - tau;
    for (int n = 0; n < calc_len; n++) {
        acc += x[n] * x[n + tau];
    }
    return acc / (float)calc_len;
}

/**
 * @brief 信号参数一站式测量：频率(ACF)、Vavg、Vmax、Vmin、Vpp
 */
void measure_signal(const uint16_t *adc_buf, uint16_t len, float fs)
{
    // 1. 平均电压 (Vavg)
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += adc_buf[i];
    }
    float avg_raw = (float)sum / (float)len;

    // 2. 5点中值滤波提取 Vmax, Vmin, Vpp
    uint16_t v_max_raw = 0;
    uint16_t v_min_raw = 4095;
    uint16_t win[5];

    for (int i = 0; i <= len - 5; i++) {
        for (int k = 0; k < 5; k++) win[k] = adc_buf[i + k];

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

    current_params.vavg = ((avg_raw - 2048.0f) / 2048.0f) * 5.0f;
    current_params.vmax = (((float)v_max_raw - 2048.0f) / 2048.0f) * 5.0f;
    current_params.vmin = (((float)v_min_raw - 2048.0f) / 2048.0f) * 5.0f;
    current_params.vpp  = current_params.vmax - current_params.vmin;

    // 3. 噪声门限检查 (纯直流判定)
    if ((v_max_raw - v_min_raw) < 20) {
        current_params.freq = 0.0f;
        return;
    }

    // 4. 去直流短时自相关 (ACF) 测频
    static float x[SAMPLE_N];
    for (uint16_t i = 0; i < len; i++) {
        x[i] = (float)adc_buf[i] - avg_raw;
    }

    float r0 = calc_acf_at(x, len, 0);
    if (r0 <= 1e-3f) {
        current_params.freq = 0.0f;
        return;
    }

    float valley_thresh = -0.15f * r0; // 必须先进入负半周波谷，过滤 tau 较小时的噪声抖动
    float peak_thresh   =  0.30f * r0; // 周期主峰必须达到足够能量

    static float r[MAX_LAG + 2];
    int peak_lag = -1;
    uint8_t passed_valley = 0;

    for (int tau = 1; tau < MAX_LAG; tau++) {
        float acc = 0.0f;
        int calc_len = len - tau;
        for (int n = 0; n < calc_len; n += 2) {
            acc += x[n] * x[n + tau];
        }
        r[tau] = acc / (float)((calc_len + 1) >> 1);

        if (!passed_valley) {
            if (r[tau] < valley_thresh) {
                passed_valley = 1;
            }
        } else {
            if (tau > 1 && r[tau - 1] > peak_thresh && r[tau] < r[tau - 1] && (tau - 1) >= MIN_LAG) {
                peak_lag = tau - 1;
                break;
            }
        }
    }

    if (peak_lag <= 0) {
        current_params.freq = 0.0f;
        return;
    }
        // 过滤非完整周期斜坡上的噪声假峰（真正周期信号主峰通常 > 0.5 * r[0]）
    if (peak_lag > 0 && r[peak_lag] < 0.3f * calc_acf_at(x, len, 0)) {
        peak_lag = -1;
    }

    if (peak_lag <= 0) {
        current_params.freq = 0.0f;
        return;
    }
    // 对极值点邻域使用全精度重算并做三点抛物线插值
    float y1 = calc_acf_at(x, len, peak_lag - 1);
    float y2 = calc_acf_at(x, len, peak_lag);
    float y3 = calc_acf_at(x, len, peak_lag + 1);
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