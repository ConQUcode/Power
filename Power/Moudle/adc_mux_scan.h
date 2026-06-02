/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc_mux_scan.h
  * @brief   CD74HC4067 通道的 ADC DMA 扫描管理模块。
  *
  * 本模块组合了以下功能：
  *   - 通过 adc_mux.c 选择 CD74HC4067 通道
  *   - ADC1 regular 序列的两个 rank
  *   - DMA normal 模式传输完成回调
  *
  * 期望 CubeMX 中的 ADC rank 顺序：
  *   rank 1 -> ADC1_IN1 -> PA0 原始采集
  *   rank 2 -> ADC1_IN2 -> PA1 误差补偿采集
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __ADC_MUX_SCAN_H__
#define __ADC_MUX_SCAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "adc.h"
#include "adc_mux.h"

typedef struct
{
  /* PA0 / ADC1_IN1 的原始 12 位 ADC 结果。 */
  uint16_t pa0_raw;

  /* PA1 / ADC1_IN2 的误差补偿 12 位 ADC 结果。 */
  uint16_t pa1_offset_raw;

  /* 已补偿的原始差值：PA0 - PA1，可能为负值。 */
  int32_t corrected_raw;
} ADCMuxScanSample_t;

/* 调试观察用：内部 16 通道采样数组的镜像，可在 Keil Watch 中展开查看。 */
extern volatile ADCMuxScanSample_t g_adcmux_debug_samples[ADCMUX1_CHANNEL_COUNT];

/* 调试观察用：当前正在更新或下一次将更新的通道样本镜像。 */
extern volatile ADCMuxScanSample_t g_adcmux_debug_current_sample;

typedef enum
{
  /* 服务函数执行完成，且未处于错误状态。 */
  ADCMUX_SCAN_OK = 0,

  /* DMA 转换正在进行，或者刚刚启动了新的转换。 */
  ADCMUX_SCAN_BUSY,

  /* ADC/DMA 初始化、启动或回调过程中发生错误。 */
  ADCMUX_SCAN_ERROR
} ADCMuxScanStatus_t;

typedef struct
{
  /* 下一次将要选择，或当前正在转换的 4067 通道号。 */
  uint8_t current_channel;

  /* 最近一次完成保存的 4067 通道号。 */
  uint8_t last_completed_channel;

  /* 一帧 0..15 通道是否已经至少采样完成一次。 */
  uint8_t frame_ready;

  /* 当前是否处于 ADC DMA 转换中。 */
  uint8_t converting;

  /* 最近一次 ADC DMA 原始缓冲区结果：rank 1 = PA0。 */
  uint16_t dma_pa0_raw;

  /* 最近一次 ADC DMA 原始缓冲区结果：rank 2 = PA1。 */
  uint16_t dma_pa1_offset_raw;

  /* 最近一次保存完成的样本。 */
  ADCMuxScanSample_t last_sample;

  /* 最近捕获到的 HAL ADC 错误码。 */
  uint32_t error_code;

  /* 16 个 4067 通道的调试镜像，方便 Keil Watch 直接展开。 */
  ADCMuxScanSample_t samples[ADCMUX1_CHANNEL_COUNT];
} ADCMuxScanDebug_t;

/* 调试观察用：Keil Watch 中直接展开该变量即可查看 ADC 采样状态。 */
extern volatile ADCMuxScanDebug_t g_adcmux_debug;

/**
  * @brief  初始化扫描器，选择多路复用器通道 0，并校准 ADC。
  * @param  hadc CubeMX 生成的 ADC 句柄，通常传入 &hadc1。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef ADCMuxScan_Init(ADC_HandleTypeDef *hadc);

/**
  * @brief  推进非阻塞扫描状态机。
  * @retval 扫描中返回 ADCMUX_SCAN_BUSY，故障时返回 ADCMUX_SCAN_ERROR。
  *
  * 需要在主循环中反复调用。该函数每次只启动一次 DMA 传输，在 DMA
  * 完成回调触发后保存结果，然后切换到下一个 4067 通道。
  */
ADCMuxScanStatus_t ADCMuxScan_Service(void);

/**
  * @brief  检查一帧完整的 16 通道数据是否已经准备好。
  * @retval 当通道 0..15 至少都采样过一次时返回 1。
  */
uint8_t ADCMuxScan_IsFrameReady(void);

/**
  * @brief  应用层读取完数据后，清除一帧完成标志。
  */
void ADCMuxScan_ClearFrameReady(void);

/**
  * @brief  获取 16 个通道采样数组的指针。
  * @retval 内部采样数组指针，数组下标对应 4067 通道号。
  */
const ADCMuxScanSample_t *ADCMuxScan_GetSamples(void);

/**
  * @brief  返回当前正在转换或下一次将要选择的通道。
  * @retval 通道号 0..15。
  */
uint8_t ADCMuxScan_GetCurrentChannel(void);

/**
  * @brief  返回扫描器最近捕获到的 ADC HAL 错误码。
  * @retval HAL ADC 错误位掩码。
  */
uint32_t ADCMuxScan_GetErrorCode(void);

/**
  * @brief  配置切换 4067 地址线后的稳定等待时间。
  * @param  settle_delay_us 启动 ADC DMA 前等待的微秒数。
  */
void ADCMuxScan_SetSettleDelayUs(uint32_t settle_delay_us);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_MUX_SCAN_H__ */
