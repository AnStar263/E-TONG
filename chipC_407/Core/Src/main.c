/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "driver_st7789.h"
#include <stdint.h>
#include <stdio.h>
#include "UI.h"
#include "ble_ssp.h"
#include "fft_math.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
  Coarse_Init = 0,
  Coarse_Wait,
  Fine_Init,
  Fine_Wait,
  Process
} State;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
DAC_HandleTypeDef hdac;

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef hdma_dcmi;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static uint8_t g_last_wave[320] = {0};
static uint8_t g_curr_wave[320] = {0};
__attribute__((aligned(4))) uint16_t adc_buf[1024];
uint8_t dma_full_flag = 0;
__attribute__((aligned(4))) uint16_t raw_adc_buf[8192];
static uint8_t g_decim_factor = 1;
volatile uint8_t rx_data = '0';
State current_state = Coarse_Init;
static float g_active_fs = FS_COARSE;

extern volatile PAGE current_page;
extern volatile PAGE next_page_req;
extern volatile uint8_t page_change_flag;
extern volatile uint8_t wave_div_change_flag;
extern volatile uint16_t g_v_scale;
extern volatile MeasureParams_t current_params;
extern volatile uint8_t s_current_tb_idx;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_DCMI_Init(void);
static void MX_DAC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void DCMI_ForceReset(void)
{
    __HAL_DCMI_DISABLE(&hdcmi);
    hdcmi.Instance->CR &= ~DCMI_CR_CAPTURE;
    HAL_DMA_Abort(&hdma_dcmi);
    hdcmi.State = HAL_DCMI_STATE_READY;
    hdcmi.ErrorCode = HAL_DCMI_ERROR_NONE;
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVFRI | DCMI_FLAG_ERRRI | DCMI_FLAG_FRAMERI);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
}

/**
 * @brief 动态修改 TIM1 输出采样率 (TIM1 时钟为 168MHz)，返回实际采样率
 */
static float TIM1_SetSampleRate(float target_fs)
{
    if (target_fs > 21000000.0f) target_fs = 21000000.0f; // 上限 21MHz (ARR=7)
    if (target_fs < 20000.0f)    target_fs = 20000.0f;    // 下限 20kHz

    uint32_t total_div = (uint32_t)(168000000.0f / target_fs + 0.5f);
    if (total_div < 8) total_div = 8;

    uint32_t psc = (total_div - 1) / 65536;
    uint32_t arr = (total_div / (psc + 1)) - 1;
    if (arr < 1) arr = 1;

    __HAL_TIM_SET_PRESCALER(&htim1, psc);
    __HAL_TIM_SET_AUTORELOAD(&htim1, arr);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (arr + 1) >> 1);
    htim1.Instance->EGR = TIM_EGR_UG; // 立即更新分频与重装载寄存器

    return 168000000.0f / ((float)(psc + 1) * (float)(arr + 1));
}

static void delay_us_systick(uint32_t us)
{
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    uint32_t told = SysTick->VAL;
    uint32_t tnow, tcnt = 0;
    uint32_t reload = SysTick->LOAD + 1U;
    while (tcnt < ticks) {
        tnow = SysTick->VAL;
        if (tnow != told) {
            tcnt += (tnow < told) ? (told - tnow) : (reload - tnow + told);
            told = tnow;
        }
    }
}

static void DCMI_StartCapture(uint16_t word_len)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
    HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)raw_adc_buf, word_len);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
    delay_us_systick(15);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_DCMI_Init();
  MX_DAC_Init();
  /* USER CODE BEGIN 2 */
  st7789_init();
  page_console();

  console_print("TEST#1");
  st7789_display_on();
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_data, 1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  static uint32_t last_frame_tick = 0;
  static uint32_t last_ble_tick = 0;
  static uint32_t wait_start_tick = 0;
  while (1) {
    /* 1. 页面切换处理 */
    if (page_change_flag) {
      page_change_flag = 0;

      // 直接关闭 DCMI 并强制中止 DMA，避免 HAL_DCMI_Stop 1000ms 超时锁死 DMA
      __HAL_DCMI_DISABLE(&hdcmi);
      hdcmi.Instance->CR &= ~DCMI_CR_CAPTURE;
      HAL_DMA_Abort(&hdma_dcmi);
      hdcmi.State = HAL_DCMI_STATE_READY;
      __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVFRI | DCMI_FLAG_ERRRI | DCMI_FLAG_FRAMERI);

      current_state = Coarse_Init;
      memset(g_last_wave, 0, sizeof(g_last_wave));

      switch (next_page_req) {
        case PAGE_CONSOLE: page_console(); break;
        case PAGE_WAVE:    page_wave();    break;
        case PAGE_PARAM:   page_param();   break;
      }
  }

    if (wave_div_change_flag && current_page == PAGE_WAVE) {
      char strtemp[4] = {'\0'};
      snprintf(strtemp, sizeof(strtemp), "%dX", g_v_scale);
      st7789_fill_rect(280, 1, 319, 23, ST7789_COLOR_BLACK);
      st7789_write_string(280, 1, strtemp, ST7789_COLOR_RED, ST7789_FONT_24);
      ui_update_topbar_volt_div();
      wave_div_change_flag = 0;
    }

    /* 2. 粗测 -> 精测状态机调度 */
    switch (current_state) {
      case Coarse_Init:
      if (HAL_GetTick() - last_frame_tick >= 33) {
        last_frame_tick = HAL_GetTick();
        g_decim_factor = 1;
        g_active_fs = TIM1_SetSampleRate(1000000.0f);
        DCMI_StartCapture(512); // 1024 点
        wait_start_tick = HAL_GetTick();
        current_state = Coarse_Wait;
      }
      break;

      case Coarse_Wait:
        if (HAL_DCMI_GetState(&hdcmi) == HAL_DCMI_STATE_READY) {
          memcpy(adc_buf, raw_adc_buf, 1024 * sizeof(uint16_t));
          measure_signal(adc_buf, 1024, g_active_fs);
          current_state = Fine_Init;
        } else if (HAL_DCMI_GetState(&hdcmi) == HAL_DCMI_STATE_ERROR ||
                  (HAL_GetTick() - wait_start_tick > 100)) {
          DCMI_ForceReset();
          current_state = Coarse_Init;
        }
        break;

        case Fine_Init:
        {
          if (current_params.freq >= 5500.0f) {
            // 高频：1x 直采，硬件时钟 >= 1 MHz
            float target_fs = current_params.freq * 128.0f;
            if (target_fs < 1000000.0f) target_fs = 1000000.0f;
            g_decim_factor = 1;
            g_active_fs = TIM1_SetSampleRate(target_fs);
            DCMI_StartCapture(512);
          } else if (s_current_tb_idx >= 10) {
            // 2ms 大时基：硬件 1 MHz 采 8192 点，8x 抽取等效 125 kHz（窗口 8.19ms）
            float hw_fs = TIM1_SetSampleRate(1000000.0f);
            g_decim_factor = 8;
            g_active_fs = hw_fs / 8.0f;
            DCMI_StartCapture(4096);
          } else {
            // 常规低频：硬件 1 MHz 采 4096 点，4x 抽取等效 250 kHz（窗口 4.10ms）
            float hw_fs = TIM1_SetSampleRate(1000000.0f);
            g_decim_factor = 4;
            g_active_fs = hw_fs / 4.0f;
            DCMI_StartCapture(2048);
          }
          wait_start_tick = HAL_GetTick();
          current_state = Fine_Wait;
        }
        break;
      case Fine_Wait:
        if (HAL_DCMI_GetState(&hdcmi) == HAL_DCMI_STATE_READY) {
          if (g_decim_factor > 1) {
            uint8_t shift = (g_decim_factor == 8) ? 3 : 2;
            for (uint16_t i = 0; i < 1024; i++) {
              uint32_t sum = 0;
              uint32_t base = i * g_decim_factor;
              for (uint8_t k = 0; k < g_decim_factor; k++) {
                sum += raw_adc_buf[base + k];
              }
              adc_buf[i] = (uint16_t)(sum >> shift);
            }
          } else {
            memcpy(adc_buf, raw_adc_buf, 1024 * sizeof(uint16_t));
          }
          current_state = Process;
        } else if (HAL_DCMI_GetState(&hdcmi) == HAL_DCMI_STATE_ERROR ||
                  (HAL_GetTick() - wait_start_tick > 100)) {
          DCMI_ForceReset();
          current_state = Coarse_Init;
        }
        break;
        case Process:
        // 1. 精测信号参数计算
        measure_signal(adc_buf, 1024, g_active_fs);
        if (g_active_fs > FS_LOW_FINE && current_params.freq < 10.0f) {
          current_state = Coarse_Init;
          break;
        }

        // 2. 周期性向上位机回传数据 (每 200ms 发送一帧)
        if (HAL_GetTick() - last_ble_tick >= 200) {
          last_ble_tick = HAL_GetTick();
          BLE_SendParams(&huart1, &current_params);
        }

        // 3. 波形页与参数页动态渲染
        if ((current_page == PAGE_WAVE || current_page == PAGE_PARAM) && !page_change_flag) {
            float sample_step = 1.0f;
            uint8_t tb_idx = ui_calc_auto_timebase(current_params.freq, g_active_fs, &sample_step);

            // 统计极值并执行施密特触发同步
            uint16_t v_min = 4095, v_max = 0;
            for (uint16_t i = 0; i < 1024; i++) {
                if (adc_buf[i] < v_min) v_min = adc_buf[i];
                if (adc_buf[i] > v_max) v_max = adc_buf[i];
            }
            uint16_t v_pp = v_max - v_min;
            uint16_t trig_start = 260; // 预留 260 点预触发历史供右移查看
            uint16_t trig_idx = trig_start;

            uint16_t period_pts = (current_params.freq > 10.0f) ? (uint16_t)(g_active_fs / current_params.freq) : 125;
            uint16_t search_limit = (uint16_t)(period_pts * 1.3f) + 10;
            if (search_limit > 400) search_limit = 400;
            uint16_t search_end = trig_start + search_limit;
            if (search_end > 1024) search_end = 1024;

            #define NOISE_GATE 40
            if (v_pp > NOISE_GATE) {
                uint16_t v_mid  = (v_min + v_max) >> 1;
                uint16_t hys    = v_pp >> 4;
                uint16_t v_low  = v_mid - hys;
                uint16_t v_high = v_mid + hys;

                uint8_t armed = 0;
                for (uint16_t i = trig_start; i < search_end; i++) {
                    if (!armed) {
                        if (adc_buf[i] < v_low) armed = 1;
                    } else {
                        if (adc_buf[i] >= v_high) {
                            trig_idx = i;
                            break;
                        }
                    }
                }
            }

            if (current_page == PAGE_WAVE) {
                ui_update_topbar_timebase(tb_idx, 0);
                for (int i = 0; i < 320; i++) {
                    int32_t sample_pos = (int32_t)trig_idx + (int32_t)((i + g_x_offset) * sample_step);
                    if (sample_pos < 0 || sample_pos > 1023) {
                        g_curr_wave[i] = 0; // 超出采样存储区留空，避免重复画 adc_buf[0]
                    } else {
                        g_curr_wave[i] = wave_adc_to_screen_y(adc_buf[sample_pos]);
                    }
                }
                screen_draw_frame(g_last_wave, g_curr_wave, 320);
            }
        }

        current_state = Coarse_Init;
        break;
    }
  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DAC Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC_Init(void)
{

  /* USER CODE BEGIN DAC_Init 0 */

  /* USER CODE END DAC_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC_Init 1 */

  /* USER CODE END DAC_Init 1 */

  /** DAC Initialization
  */
  hdac.Instance = DAC;
  if (HAL_DAC_Init(&hdac) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT2 config
  */
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC_Init 2 */

  /* USER CODE END DAC_Init 2 */

}

/**
  * @brief DCMI Initialization Function
  * @param None
  * @retval None
  */
static void MX_DCMI_Init(void)
{

  /* USER CODE BEGIN DCMI_Init 0 */

  /* USER CODE END DCMI_Init 0 */

  /* USER CODE BEGIN DCMI_Init 1 */

  /* USER CODE END DCMI_Init 1 */
  hdcmi.Instance = DCMI;
  hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
  hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_FALLING;
  hdcmi.Init.VSPolarity = DCMI_VSPOLARITY_LOW;
  hdcmi.Init.HSPolarity = DCMI_HSPOLARITY_HIGH;
  hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
  hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_12B;
  hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
  if (HAL_DCMI_Init(&hdcmi) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DCMI_Init 2 */

  /* USER CODE END DCMI_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 79;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 4;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_RST_GPIO_Port, SPI_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_DC_GPIO_Port, SPI_DC_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_BL_GPIO_Port, SPI_BL_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : SPI_RST_Pin SPI_DC_Pin */
  GPIO_InitStruct.Pin = SPI_RST_Pin|SPI_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI_CS_Pin */
  GPIO_InitStruct.Pin = SPI_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(SPI_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI_BL_Pin */
  GPIO_InitStruct.Pin = SPI_BL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI_BL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */


void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi) {
  dma_full_flag = 1;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
