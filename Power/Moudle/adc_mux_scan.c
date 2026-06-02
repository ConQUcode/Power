/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc_mux_scan.c
  * @brief   CD74HC4067 通道的 ADC DMA 扫描管理模块。
  *
  * 每个 4067 通道的扫描流程：
  *   1. 驱动 S0..S3，选择通道 N。
  *   2. 等待短暂的模拟通道稳定时间。
  *   3. 启动 ADC1 DMA，采集两个 regular rank。
  *   4. DMA 完成回调只设置完成标志。
  *   5. 主循环中的服务函数保存 rank 结果，并推进到通道 N+1。
  *
  * 回调函数只置位标志；所有状态转换和数据拷贝都在 ADCMuxScan_Service()
  * 中完成，即普通主循环上下文。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "adc_mux_scan.h"

#define ADCMUX_SCAN_ADC_RANK_COUNT        (2U)  /* CubeMX ADC1 序列包含 IN1 和 IN2。 */
#define ADCMUX_SCAN_DEFAULT_SETTLE_US     (20U) /* 保守的 4067 初始稳定等待时间。 */

typedef enum
{
  /* 当前没有 ADC 转换运行，服务函数可以选择通道并启动 DMA。 */
  ADCMUX_SCAN_STATE_IDLE = 0,

  /* ADC DMA 正在运行，服务函数等待回调置位标志。 */
  ADCMUX_SCAN_STATE_CONVERTING,

  /* 初始化、启动或回调中出现致命错误，扫描器保持停止。 */
  ADCMUX_SCAN_STATE_ERROR
} ADCMuxScanState_t;

static ADC_HandleTypeDef *adcmux_scan_hadc; /* 当前扫描器使用的 ADC 实例。 */
static ADCMuxScanState_t adcmux_scan_state = ADCMUX_SCAN_STATE_IDLE;
static ADCMuxScanSample_t adcmux_scan_samples[ADCMUX1_CHANNEL_COUNT];
static uint16_t adcmux_scan_dma_buffer[ADCMUX_SCAN_ADC_RANK_COUNT];
static volatile uint8_t adcmux_scan_conversion_done; /* 由 ADC DMA 完成回调写入。 */
static volatile uint8_t adcmux_scan_error;           /* 由 ADC 错误回调写入。 */
static volatile uint8_t adcmux_scan_frame_ready;     /* 保存完通道 15 后置位。 */
static uint8_t adcmux_scan_current_channel;
static uint32_t adcmux_scan_settle_delay_us = ADCMUX_SCAN_DEFAULT_SETTLE_US;
static uint32_t adcmux_scan_error_code;

volatile ADCMuxScanSample_t g_adcmux_debug_samples[ADCMUX1_CHANNEL_COUNT];
volatile ADCMuxScanSample_t g_adcmux_debug_current_sample;
volatile ADCMuxScanDebug_t g_adcmux_debug;

static void ADCMuxScan_UpdateDebugSample(uint8_t channel)
{
  g_adcmux_debug_samples[channel].pa0_raw = adcmux_scan_samples[channel].pa0_raw;
  g_adcmux_debug_samples[channel].pa1_offset_raw = adcmux_scan_samples[channel].pa1_offset_raw;
  g_adcmux_debug_samples[channel].corrected_raw = adcmux_scan_samples[channel].corrected_raw;

  g_adcmux_debug.samples[channel].pa0_raw = adcmux_scan_samples[channel].pa0_raw;
  g_adcmux_debug.samples[channel].pa1_offset_raw = adcmux_scan_samples[channel].pa1_offset_raw;
  g_adcmux_debug.samples[channel].corrected_raw = adcmux_scan_samples[channel].corrected_raw;
}

HAL_StatusTypeDef ADCMuxScan_Init(ADC_HandleTypeDef *hadc)
{
  HAL_StatusTypeDef status;

  if (hadc == NULL)
  {
    return HAL_ERROR;
  }

  adcmux_scan_hadc = hadc;
  adcmux_scan_state = ADCMUX_SCAN_STATE_IDLE;
  adcmux_scan_current_channel = 0U;
  adcmux_scan_conversion_done = 0U;
  adcmux_scan_error = 0U;
  adcmux_scan_frame_ready = 0U;
  adcmux_scan_error_code = 0U;
  g_adcmux_debug_current_sample.pa0_raw = 0U;
  g_adcmux_debug_current_sample.pa1_offset_raw = 0U;
  g_adcmux_debug_current_sample.corrected_raw = 0;
  g_adcmux_debug.current_channel = adcmux_scan_current_channel;
  g_adcmux_debug.last_completed_channel = 0U;
  g_adcmux_debug.frame_ready = adcmux_scan_frame_ready;
  g_adcmux_debug.converting = 0U;
  g_adcmux_debug.dma_pa0_raw = 0U;
  g_adcmux_debug.dma_pa1_offset_raw = 0U;
  g_adcmux_debug.last_sample.pa0_raw = 0U;
  g_adcmux_debug.last_sample.pa1_offset_raw = 0U;
  g_adcmux_debug.last_sample.corrected_raw = 0;
  g_adcmux_debug.error_code = adcmux_scan_error_code;

  ADCMUX1_Init();

  /*
   * 在 MX_ADC1_Init() 之后、第一次 DMA 转换之前执行一次 ADC 校准。
   * 这对 STM32G4 ADC 的偏移稳定性是必要的。
   */
  status = HAL_ADCEx_Calibration_Start(adcmux_scan_hadc, ADC_SINGLE_ENDED);
  if (status != HAL_OK)
  {
    adcmux_scan_state = ADCMUX_SCAN_STATE_ERROR;
    adcmux_scan_error_code = HAL_ADC_GetError(adcmux_scan_hadc);
  }

  return status;
}

void ADCMuxScan_SetSettleDelayUs(uint32_t settle_delay_us)
{
  adcmux_scan_settle_delay_us = settle_delay_us;
}

ADCMuxScanStatus_t ADCMuxScan_Service(void)
{
  HAL_StatusTypeDef status;

  if (adcmux_scan_hadc == NULL)
  {
    adcmux_scan_state = ADCMUX_SCAN_STATE_ERROR;
    adcmux_scan_error_code = HAL_ADC_ERROR_INTERNAL;
    return ADCMUX_SCAN_ERROR;
  }

  if ((adcmux_scan_state == ADCMUX_SCAN_STATE_CONVERTING) && (adcmux_scan_conversion_done != 0U))
  {
    /*
     * DMA 缓冲区顺序遵循 CubeMX 中的 ADC rank 顺序：
     *   buffer[0] = rank 1 = ADC1_IN1 = PA0 原始采集
     *   buffer[1] = rank 2 = ADC1_IN2 = PA1 误差补偿采集
     */
    adcmux_scan_samples[adcmux_scan_current_channel].pa0_raw = adcmux_scan_dma_buffer[0];
    adcmux_scan_samples[adcmux_scan_current_channel].pa1_offset_raw = adcmux_scan_dma_buffer[1];
    adcmux_scan_samples[adcmux_scan_current_channel].corrected_raw =
        (int32_t)adcmux_scan_dma_buffer[0] - (int32_t)adcmux_scan_dma_buffer[1];
    g_adcmux_debug.dma_pa0_raw = adcmux_scan_dma_buffer[0];
    g_adcmux_debug.dma_pa1_offset_raw = adcmux_scan_dma_buffer[1];
    g_adcmux_debug.last_completed_channel = adcmux_scan_current_channel;
    g_adcmux_debug.last_sample.pa0_raw = adcmux_scan_samples[adcmux_scan_current_channel].pa0_raw;
    g_adcmux_debug.last_sample.pa1_offset_raw = adcmux_scan_samples[adcmux_scan_current_channel].pa1_offset_raw;
    g_adcmux_debug.last_sample.corrected_raw = adcmux_scan_samples[adcmux_scan_current_channel].corrected_raw;
    ADCMuxScan_UpdateDebugSample(adcmux_scan_current_channel);

    adcmux_scan_current_channel++;
    if (adcmux_scan_current_channel >= ADCMUX1_CHANNEL_COUNT)
    {
      adcmux_scan_current_channel = 0U;
      adcmux_scan_frame_ready = 1U;
    }
    g_adcmux_debug_current_sample.pa0_raw = adcmux_scan_samples[adcmux_scan_current_channel].pa0_raw;
    g_adcmux_debug_current_sample.pa1_offset_raw = adcmux_scan_samples[adcmux_scan_current_channel].pa1_offset_raw;
    g_adcmux_debug_current_sample.corrected_raw = adcmux_scan_samples[adcmux_scan_current_channel].corrected_raw;
    g_adcmux_debug.current_channel = adcmux_scan_current_channel;
    g_adcmux_debug.frame_ready = adcmux_scan_frame_ready;
    g_adcmux_debug.converting = 0U;

    adcmux_scan_state = ADCMUX_SCAN_STATE_IDLE;
  }

  if ((adcmux_scan_state == ADCMUX_SCAN_STATE_CONVERTING) && (adcmux_scan_error != 0U))
  {
    adcmux_scan_state = ADCMUX_SCAN_STATE_ERROR;
    adcmux_scan_error_code = HAL_ADC_GetError(adcmux_scan_hadc);
    g_adcmux_debug.error_code = adcmux_scan_error_code;
    g_adcmux_debug.converting = 0U;
  }

  if (adcmux_scan_state == ADCMUX_SCAN_STATE_ERROR)
  {
    return ADCMUX_SCAN_ERROR;
  }

  if (adcmux_scan_state == ADCMUX_SCAN_STATE_CONVERTING)
  {
    return ADCMUX_SCAN_BUSY;
  }

  ADCMUX1_SelectChannel(adcmux_scan_current_channel);
  ADCMUX1_DelayUs(adcmux_scan_settle_delay_us);

  /*
   * DMA 配置为 normal 模式。每次由服务函数启动的传输只采集
   * ADCMUX_SCAN_ADC_RANK_COUNT 个样本，然后触发 HAL 完成回调。
   */
  adcmux_scan_conversion_done = 0U;
  adcmux_scan_error = 0U;
  g_adcmux_debug.current_channel = adcmux_scan_current_channel;

  status = HAL_ADC_Start_DMA(adcmux_scan_hadc, (uint32_t *)adcmux_scan_dma_buffer,
                             ADCMUX_SCAN_ADC_RANK_COUNT);
  if (status != HAL_OK)
  {
    adcmux_scan_state = ADCMUX_SCAN_STATE_ERROR;
    adcmux_scan_error_code = HAL_ADC_GetError(adcmux_scan_hadc);
    g_adcmux_debug.error_code = adcmux_scan_error_code;
    g_adcmux_debug.converting = 0U;
    return ADCMUX_SCAN_ERROR;
  }

  adcmux_scan_state = ADCMUX_SCAN_STATE_CONVERTING;
  g_adcmux_debug.converting = 1U;
  return ADCMUX_SCAN_BUSY;
}

uint8_t ADCMuxScan_IsFrameReady(void)
{
  /* 该标志会保持置位，直到应用层显式调用清除函数。 */
  return adcmux_scan_frame_ready;
}

void ADCMuxScan_ClearFrameReady(void)
{
  adcmux_scan_frame_ready = 0U;
}

const ADCMuxScanSample_t *ADCMuxScan_GetSamples(void)
{
  return adcmux_scan_samples;
}

uint8_t ADCMuxScan_GetCurrentChannel(void)
{
  return adcmux_scan_current_channel;
}

uint32_t ADCMuxScan_GetErrorCode(void)
{
  return adcmux_scan_error_code;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc == adcmux_scan_hadc)
  {
    /* ISR 中只做最少工作；数据拷贝在 ADCMuxScan_Service() 中完成。 */
    adcmux_scan_conversion_done = 1U;
  }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc == adcmux_scan_hadc)
  {
    /* 主循环服务函数会将该标志转换为 ADCMUX_SCAN_STATE_ERROR 状态。 */
    adcmux_scan_error = 1U;
  }
}
