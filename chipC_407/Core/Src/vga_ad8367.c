#include "vga_ad8367.h"
#include <math.h>

// 微秒级延时：用于等待 DAC 建立与控制引脚 RC 响应
static void VGA_Delay_Us(uint32_t us) {
    // 假设主频 168MHz，简易粗略延时，可替换为 DWT->CYCCNT 精确延时
    volatile uint32_t count = us * 28;
    while (count--) {
        __NOP();
    }
}

void VGA_Init(VGA_Handle_t *vga, DAC_HandleTypeDef *hdac, uint32_t channel) {
    vga->hdac = hdac;
    vga->channel = channel;
    
    HAL_DAC_Start(vga->hdac, vga->channel);
    // 初始设置到中等偏低安全增益 (0 dB)，防止初始信号过大打坏后级
    VGA_SetGain_dB(vga, 0.0f);
}

void VGA_SetGain_dB(VGA_Handle_t *vga, float gain_db) {
    // 1. 增益限幅
    if (gain_db < AD8367_GAIN_MIN) gain_db = AD8367_GAIN_MIN;
    if (gain_db > AD8367_GAIN_MAX) gain_db = AD8367_GAIN_MAX;

    vga->current_gain_db = gain_db;
    // 计算并更新线性电压倍数 Av = 10^(Gain_dB / 20)
    vga->linear_gain = powf(10.0f, gain_db / 20.0f);

    // 2. 换算控制电压: Gain(dB) = 50 * Vctrl - 5.0  =>  Vctrl = (Gain + 5.0) / 50.0
    float v_ctrl = (gain_db + 5.0f) / 50.0f;
    if (v_ctrl < VCTRL_MIN) v_ctrl = VCTRL_MIN;
    if (v_ctrl > VCTRL_MAX) v_ctrl = VCTRL_MAX;

    // 3. 换算 DAC 码值并写入寄存器
    uint16_t dac_code = (uint16_t)roundf((v_ctrl / V_REF_DAC) * DAC_MAX_CODE);
    HAL_DAC_SetValue(vga->hdac, vga->channel, DAC_ALIGN_12B_R, dac_code);
}

float VGA_GetGain_dB(const VGA_Handle_t *vga) {
    return vga->current_gain_db;
}

float VGA_GetLinearGain(const VGA_Handle_t *vga) {
    return vga->linear_gain;
}

bool VGA_FastLock(VGA_Handle_t *vga, float target_vpp, float tolerance, 
                   uint8_t max_iter, float (*get_vpp_fn)(void)) {
    if (get_vpp_fn == NULL) return false;

    for (uint8_t i = 0; i < max_iter; i++) {
        // 等待 DAC 输出及硬件 RC 建立 (通常 20~50us 足够)
        VGA_Delay_Us(30);

        // 1. 调用片外模块测得的实时 Vpp
        float vpp = get_vpp_fn();

        // 2. 信号异常检测 (断线或低于检波门限)
        if (vpp < 0.015f) {
            // 输入过小，推至最大增益后返回失败
            VGA_SetGain_dB(vga, AD8367_GAIN_MAX);
            return false;
        }

        // 3. 误差命中判定
        if (fabsf(vpp - target_vpp) <= tolerance) {
            return true;
        }

        // 4. 利用 dB 线性单步对数收敛: Delta_G = 20 * log10(Target / Measured)
        float delta_gain_db = 20.0f * log10f(target_vpp / vpp);
        float next_gain_db  = vga->current_gain_db + delta_gain_db;

        // 5. 判断是否越界 (若所需增益超出芯片动态范围，设到极限并跳出)
        if (next_gain_db >= AD8367_GAIN_MAX) {
            VGA_SetGain_dB(vga, AD8367_GAIN_MAX);
            break;
        } else if (next_gain_db <= AD8367_GAIN_MIN) {
            VGA_SetGain_dB(vga, AD8367_GAIN_MIN);
            break;
        }

        VGA_SetGain_dB(vga, next_gain_db);
    }

    // 最终验证一次是否处于容差内
    VGA_Delay_Us(30);
    return (fabsf(get_vpp_fn() - target_vpp) <= tolerance);
}