#ifndef __VGA_AD8367_H
#define __VGA_AD8367_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>

#define V_REF_DAC           3.30f     // STM32 DAC 参考基准电压 (V)
#define DAC_MAX_CODE        4095.0f

/* AD8367 规格常数 (MODE 接高电平，正斜率 50 dB/V) */
#define AD8367_GAIN_MIN    -2.5f      // 芯片标称最小增益 (dB)
#define AD8367_GAIN_MAX     42.5f     // 芯片标称最大增益 (dB)
#define VCTRL_MIN           0.05f     // 线性工作区控制电压下限 (V)
#define VCTRL_MAX           0.95f     // 线性工作区控制电压上限 (V)

typedef struct {
    DAC_HandleTypeDef *hdac;
    uint32_t channel;
    float current_gain_db;    // 当前增益 (dB)
    float linear_gain;        // 当前线性电压放大倍数 (Av = Vout / Vin)
} VGA_Handle_t;

// 函数声明
void  VGA_Init(VGA_Handle_t *vga, DAC_HandleTypeDef *hdac, uint32_t channel);
void  VGA_SetGain_dB(VGA_Handle_t *vga, float gain_db);
float VGA_GetGain_dB(const VGA_Handle_t *vga);
float VGA_GetLinearGain(const VGA_Handle_t *vga);

/**
 * @brief  快速锁定输出峰峰值至目标值
 * @param  vga          VGA 结构体指针
 * @param  target_vpp   目标峰峰值 (如 1.0f)
 * @param  tolerance    允许误差绝对值 (如 0.02f)
 * @param  max_iter     最大迭代步数 (推荐 2~3 次)
 * @param  get_vpp_fn   外部采集模块的测 Vpp 函数指针
 * @retval true 成功锁定, false 超出动态范围或无信号
 */
bool  VGA_FastLock(VGA_Handle_t *vga, float target_vpp, float tolerance, 
                   uint8_t max_iter, float (*get_vpp_fn)(void));

#endif
