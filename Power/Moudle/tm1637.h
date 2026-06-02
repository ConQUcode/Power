/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tm1637.h
  * @brief   TM1637 四位数码管显示驱动。
  *
  * 本模块使用 CubeMX 生成的 GPIO 标签：
  *   TM1637_CLK -> PC6
  *   TM1637_DIO -> PC7
  *
  * TM1637 采用类似 I2C 的双线时序，但不是标准 I2C 外设。本驱动使用
  * GPIO 阻塞式 bit-bang，适合在主循环或低频显示刷新任务中调用。
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __TM1637_H__
#define __TM1637_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define TM1637_DIGIT_COUNT  (4U)

/* 原始段码位定义，bit7 通常用于小数点或冒号。 */
#define TM1637_SEG_A        (0x01U)
#define TM1637_SEG_B        (0x02U)
#define TM1637_SEG_C        (0x04U)
#define TM1637_SEG_D        (0x08U)
#define TM1637_SEG_E        (0x10U)
#define TM1637_SEG_F        (0x20U)
#define TM1637_SEG_G        (0x40U)
#define TM1637_SEG_DOT      (0x80U)

/**
  * @brief  初始化 TM1637 总线并清空显示。
  * @retval HAL 状态。
  *
  * 调用前需要先执行 MX_GPIO_Init()，确保 PC6/PC7 已经配置为输出并置高。
  */
HAL_StatusTypeDef TM1637_Init(void);

/**
  * @brief  修改 TM1637 bit-bang 半周期延时。
  * @param  delay_us 延时时间，单位微秒，0 会被修正为 1。
  *
  * 默认值为 3 us。线长较长或模块响应不稳定时可适当增大。
  */
void TM1637_SetDelayUs(uint32_t delay_us);

/**
  * @brief  设置显示亮度和开关状态。
  * @param  brightness 亮度等级 0..7，高位会被忽略。
  * @param  display_on 1 打开显示，0 关闭显示但不清除显存。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef TM1637_SetBrightness(uint8_t brightness, uint8_t display_on);

/**
  * @brief  清空 4 位显示内容。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef TM1637_Clear(void);

/**
  * @brief  从指定位置写入原始段码。
  * @param  position 起始位置 0..3。
  * @param  segments 段码数组，每个元素直接对应 TM1637 显示 RAM。
  * @param  length 写入长度，超出第 4 位的部分会被忽略。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef TM1637_DisplayRaw(uint8_t position, const uint8_t *segments, uint8_t length);

/**
  * @brief  显示 0..9、A..F、横杠和空格等常用字符。
  * @param  text 至少包含 length 个字符的字符串指针。
  * @param  length 待显示字符数，最多显示 4 位。
  * @param  dot_mask 小数点/冒号掩码，bit0 对应第 0 位，bit3 对应第 3 位。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef TM1637_DisplayText(const char *text, uint8_t length, uint8_t dot_mask);

/**
  * @brief  显示十进制整数。
  * @param  value 显示范围 -999..9999，超出范围时显示横杠。
  * @param  show_leading_zero 非 0 时补前导 0，0 时前导位留空。
  * @param  dot_mask 小数点/冒号掩码，bit0 对应第 0 位，bit3 对应第 3 位。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef TM1637_DisplayNumber(int16_t value, uint8_t show_leading_zero, uint8_t dot_mask);

/**
  * @brief  将 0..9 编码为七段数码管段码。
  * @param  digit 十进制数字。
  * @retval digit 有效时返回段码，否则返回 0。
  */
uint8_t TM1637_EncodeDigit(uint8_t digit);

/**
  * @brief  将常用字符编码为七段数码管段码。
  * @param  ch 待编码字符，支持 0..9、A..F、a..f、横杠、下划线和空格。
  * @retval 字符对应段码，未知字符返回 0。
  */
uint8_t TM1637_EncodeChar(char ch);

#ifdef __cplusplus
}
#endif

#endif /* __TM1637_H__ */
