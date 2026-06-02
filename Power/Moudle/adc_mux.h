/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc_mux.h
  * @brief   CD74HC4067 地址线控制。
  *
  * CD74HC4067 使用 S0..S3 四根地址输入线，从 16 路模拟通道中选择一路。
  * 本项目中 EN 引脚已由硬件拉低使能，因此本模块只负责驱动地址线，
  * 不额外提供使能/关闭接口。
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __ADC_MUX_H__
#define __ADC_MUX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* CD74HC4067 共有 C0..C15 十六个通道。 */
#define ADCMUX1_CHANNEL_COUNT  (16U)

/**
  * @brief  选择默认通道 0。
  */
void ADCMUX1_Init(void);

/**
  * @brief  选择 CD74HC4067 的 16 路通道之一。
  * @param  channel 通道号 0..15，高位会被忽略。
  *
  * 地址位映射：
  *   bit0 -> ADCMUX1_S0 -> PB11
  *   bit1 -> ADCMUX1_S1 -> PB12
  *   bit2 -> ADCMUX1_S2 -> PB13
  *   bit3 -> ADCMUX1_S3 -> PB14
  */
void ADCMUX1_SelectChannel(uint8_t channel);

/**
  * @brief  返回最近一次由 ADCMUX1_SelectChannel() 写入的通道号。
  * @retval 通道号 0..15。
  */
uint8_t ADCMUX1_GetSelectedChannel(void);

/**
  * @brief  修改 4067 地址线后使用的阻塞式微秒延时。
  * @param  delay_us 延时时间，单位为微秒。
  */
void ADCMUX1_DelayUs(uint32_t delay_us);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_MUX_H__ */
