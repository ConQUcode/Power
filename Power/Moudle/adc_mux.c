/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc_mux.c
  * @brief   CD74HC4067 地址线控制。
  *
  * 硬件映射来自 CubeMX 的 GPIO 标签：
  *   ADCMUX1_S0 -> 通道地址 bit0
  *   ADCMUX1_S1 -> 通道地址 bit1
  *   ADCMUX1_S2 -> 通道地址 bit2
  *   ADCMUX1_S3 -> 通道地址 bit3
  *
  * EN 已经由外部硬件拉低，因此每次更新地址线后，模拟开关会立即选择
  * 对应通道。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "adc_mux.h"

static uint8_t adcmux1_selected_channel; /* 当前 4067 地址的软件副本。 */

static GPIO_PinState ADCMUX1_PinState(uint8_t channel, uint8_t bit_mask)
{
  return ((channel & bit_mask) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

static uint8_t ADCMUX1_DWTDelayAvailable(void)
{
  /*
   * DWT->CYCCNT 在 Cortex-M4 上提供稳定的周期计数，适合用于较短的
   * 模拟通道稳定等待。如果无法启用，则延时函数会退回到 NOP 循环。
   */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  return ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ? 1U : 0U;
}

void ADCMUX1_DelayUs(uint32_t delay_us)
{
  uint32_t start_tick;
  uint32_t wait_cycles;
  volatile uint32_t fallback_count;

  if (delay_us == 0U)
  {
    return;
  }

  if (ADCMUX1_DWTDelayAvailable() != 0U)
  {
    start_tick = DWT->CYCCNT;
    wait_cycles = (SystemCoreClock / 1000000U) * delay_us;
    /* 使用无符号减法可以正确处理 CYCCNT 计数回绕。 */
    while ((DWT->CYCCNT - start_tick) < wait_cycles)
    {
    }
    return;
  }

  fallback_count = (SystemCoreClock / 12000000U) * delay_us;
  while (fallback_count-- > 0U)
  {
    __NOP();
  }
}

void ADCMUX1_Init(void)
{
  /* CubeMX 已将所有地址脚初始化为低电平，这里再次明确选择通道 0。 */
  adcmux1_selected_channel = 0U;
  ADCMUX1_SelectChannel(adcmux1_selected_channel);
}

void ADCMUX1_SelectChannel(uint8_t channel)
{
  channel &= 0x0FU;

  /*
   * CD74HC4067 二进制寻址：
   *   channel 0  = 0000
   *   channel 1  = 0001
   *   ...
   *   channel 15 = 1111
   */
  HAL_GPIO_WritePin(ADCMUX1_S0_GPIO_Port, ADCMUX1_S0_Pin, ADCMUX1_PinState(channel, 0x01U));
  HAL_GPIO_WritePin(ADCMUX1_S1_GPIO_Port, ADCMUX1_S1_Pin, ADCMUX1_PinState(channel, 0x02U));
  HAL_GPIO_WritePin(ADCMUX1_S2_GPIO_Port, ADCMUX1_S2_Pin, ADCMUX1_PinState(channel, 0x04U));
  HAL_GPIO_WritePin(ADCMUX1_S3_GPIO_Port, ADCMUX1_S3_Pin, ADCMUX1_PinState(channel, 0x08U));

  adcmux1_selected_channel = channel;
}

uint8_t ADCMUX1_GetSelectedChannel(void)
{
  return adcmux1_selected_channel;
}
