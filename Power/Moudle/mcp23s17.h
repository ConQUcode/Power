/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mcp23s17.h
  * @brief   MCP23S17 SPI GPIO 扩展芯片驱动。
  *
  * 本驱动默认一片 MCP23S17 连接在 SPI1 上，并使用 CubeMX 生成的
  * SPI1_CS1 和 SPI1_RST 作为软件片选与硬件复位引脚。驱动使用
  * BANK=0 寄存器布局，此时 A/B 两组端口寄存器连续排列，可以按
  * 一个 16 位 GPIO 端口处理。
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MCP23S17_H__
#define __MCP23S17_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spi.h"

/*
 * 公开 API 使用的 16 位 GPIO 位掩码。
 *
 * bit0..bit7   对应 MCP23S17 的 GPA0..GPA7。
 * bit8..bit15  对应 MCP23S17 的 GPB0..GPB7。
 */
#define MCP23S17_PIN_0     (0x0001U)
#define MCP23S17_PIN_1     (0x0002U)
#define MCP23S17_PIN_2     (0x0004U)
#define MCP23S17_PIN_3     (0x0008U)
#define MCP23S17_PIN_4     (0x0010U)
#define MCP23S17_PIN_5     (0x0020U)
#define MCP23S17_PIN_6     (0x0040U)
#define MCP23S17_PIN_7     (0x0080U)
#define MCP23S17_PIN_8     (0x0100U)
#define MCP23S17_PIN_9     (0x0200U)
#define MCP23S17_PIN_10    (0x0400U)
#define MCP23S17_PIN_11    (0x0800U)
#define MCP23S17_PIN_12    (0x1000U)
#define MCP23S17_PIN_13    (0x2000U)
#define MCP23S17_PIN_14    (0x4000U)
#define MCP23S17_PIN_15    (0x8000U)
#define MCP23S17_PIN_ALL   (0xFFFFU)

/**
  * @brief  初始化 MCP23S17，并将所有扩展 GPIO 保持为输入状态。
  * @param  hspi CubeMX 生成的 SPI 句柄，通常传入 &hspi1。
  * @param  hardware_address A2..A0 硬件地址值，范围 0..7。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_Init(SPI_HandleTypeDef *hspi, uint8_t hardware_address);

/**
  * @brief  将 MCP23S17 复位脚拉低形成复位脉冲，然后释放为高电平。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_Reset(void);

/**
  * @brief  读取一个 MCP23S17 寄存器。
  * @param  reg BANK=0 地址模式下的寄存器地址。
  * @param  value 用于保存寄存器值的指针。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_ReadReg(uint8_t reg, uint8_t *value);

/**
  * @brief  写入一个 MCP23S17 寄存器。
  * @param  reg BANK=0 地址模式下的寄存器地址。
  * @param  value 要写入的寄存器值。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_WriteReg(uint8_t reg, uint8_t value);

/**
  * @brief  将两个相邻的 8 位寄存器读成一个 16 位小端值。
  * @param  reg_lsb 低字节寄存器地址，例如 IODIRA 或 GPIOA。
  * @param  value 用于保存 A/B 端口合并值的指针。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_ReadReg16(uint8_t reg_lsb, uint16_t *value);

/**
  * @brief  将一个 16 位小端值写入两个相邻的 8 位寄存器。
  * @param  reg_lsb 低字节寄存器地址，例如 IODIRA 或 OLATA。
  * @param  value A/B 端口合并值。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_WriteReg16(uint8_t reg_lsb, uint16_t value);

/**
  * @brief  配置扩展 GPIO 的方向、内部上拉和输出锁存值。
  * @param  input_mask 方向掩码：bit=1 表示输入，bit=0 表示输出。
  * @param  pullup_mask 上拉掩码：bit=1 打开典型 100 kOhm 内部上拉。
  * @param  output_value 输出脚的初始输出锁存值。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_ConfigureGPIO(uint16_t input_mask, uint16_t pullup_mask, uint16_t output_value);

/**
  * @brief  从 GPIOA/GPIOB 读取当前引脚电平。
  * @param  value 用于保存 16 位引脚状态的指针。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_ReadGPIO(uint16_t *value);

/**
  * @brief  写入全部 16 位输出锁存值。
  * @param  value 新的 16 位输出锁存值。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_WriteGPIO(uint16_t value);

/**
  * @brief  将指定输出锁存位置 1，不影响其他位。
  * @param  pin_mask 需要置 1 的位掩码。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_SetPins(uint16_t pin_mask);

/**
  * @brief  将指定输出锁存位清 0，不影响其他位。
  * @param  pin_mask 需要清 0 的位掩码。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_ResetPins(uint16_t pin_mask);

/**
  * @brief  翻转指定输出锁存位，不影响其他位。
  * @param  pin_mask 需要翻转的位掩码。
  * @retval HAL 状态。
  */
HAL_StatusTypeDef MCP23S17_TogglePins(uint16_t pin_mask);

#ifdef __cplusplus
}
#endif

#endif /* __MCP23S17_H__ */
