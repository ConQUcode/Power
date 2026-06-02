/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    mcp23s17.c
  * @brief   MCP23S17 SPI GPIO 扩展芯片驱动。
  *
  * SPI 传输格式：
  *   写操作：opcode, register, data
  *   读操作：opcode, register, dummy -> 数据在 dummy 字节期间返回
  *
  * opcode 格式为 0100 A2 A1 A0 R/W。驱动在初始化时打开 IOCON.HAEN，
  * 使 MCP23S17 的 A2..A0 硬件地址位生效。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "mcp23s17.h"

#define MCP23S17_SPI_TIMEOUT_MS       (10U)  /* 短 3 字节阻塞式 SPI 传输超时时间。 */
#define MCP23S17_OPCODE_BASE          (0x40U) /* MCP23S17 数据手册定义的 0100xxx0 基础 opcode。 */
#define MCP23S17_OPCODE_READ_BIT      (0x01U) /* opcode 的 R/W 读写选择位。 */

/* BANK=0 寄存器地址。B 端口寄存器位于 A 端口地址 + 1 的位置。 */
#define MCP23S17_REG_IODIRA           (0x00U) /* 方向寄存器：1=输入，0=输出。 */
#define MCP23S17_REG_IODIRB           (0x01U)
#define MCP23S17_REG_GPPUA            (0x0CU) /* 内部上拉使能寄存器。 */
#define MCP23S17_REG_GPIOA            (0x12U) /* 当前引脚电平寄存器。 */
#define MCP23S17_REG_OLATA            (0x14U) /* 输出锁存寄存器。 */
#define MCP23S17_REG_IOCON            (0x0AU) /* 器件配置寄存器。 */

#define MCP23S17_IOCON_HAEN           (0x08U) /* 使能 SPI 硬件地址位 A2..A0。 */

static SPI_HandleTypeDef *mcp23s17_hspi; /* CubeMX 管理的 SPI 总线句柄。 */
static uint8_t mcp23s17_address;         /* 当前使用的 3 位 MCP23S17 硬件地址。 */
static uint16_t mcp23s17_output_latch;   /* OLATA/OLATB 缓存，用于 set/reset/toggle 接口。 */

static uint8_t MCP23S17_Opcode(uint8_t read)
{
  /* opcode 格式：0b0100_A2A1A0_RnW。 */
  return (uint8_t)(MCP23S17_OPCODE_BASE | ((mcp23s17_address & 0x07U) << 1) |
                   (read ? MCP23S17_OPCODE_READ_BIT : 0U));
}

static void MCP23S17_Select(void)
{
  /* MCP23S17 在 CS 为低电平期间采样 opcode、寄存器地址和数据。 */
  HAL_GPIO_WritePin(SPI1_CS1_GPIO_Port, SPI1_CS1_Pin, GPIO_PIN_RESET);
}

static void MCP23S17_Deselect(void)
{
  /* CS 拉高表示当前 SPI 命令结束。 */
  HAL_GPIO_WritePin(SPI1_CS1_GPIO_Port, SPI1_CS1_Pin, GPIO_PIN_SET);
}

HAL_StatusTypeDef MCP23S17_Reset(void)
{
  /*
   * RESET 为低电平有效。复位期间保持 CS 为高电平，避免芯片在内部状态
   * 清除过程中误解析总线上的其他活动。
   */
  HAL_GPIO_WritePin(SPI1_CS1_GPIO_Port, SPI1_CS1_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SPI1_RST_GPIO_Port, SPI1_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(SPI1_RST_GPIO_Port, SPI1_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(2U);

  return HAL_OK;
}

HAL_StatusTypeDef MCP23S17_Init(SPI_HandleTypeDef *hspi, uint8_t hardware_address)
{
  HAL_StatusTypeDef status;
  uint8_t verify = 0U;

  if (hspi == NULL)
  {
    return HAL_ERROR;
  }

  mcp23s17_hspi = hspi;
  /*
   * 复位后 HAEN 默认关闭，器件只响应地址 0。这里先用地址 0 打开 HAEN，
   * 然后再切换到调用者传入的硬件地址，供后续所有事务使用。
   */
  mcp23s17_address = 0U;
  mcp23s17_output_latch = 0U;

  status = MCP23S17_Reset();
  if (status != HAL_OK)
  {
    return status;
  }

  status = MCP23S17_WriteReg(MCP23S17_REG_IOCON, MCP23S17_IOCON_HAEN);
  if (status != HAL_OK)
  {
    return status;
  }

  mcp23s17_address = hardware_address & 0x07U;

  status = MCP23S17_ReadReg(MCP23S17_REG_IOCON, &verify);
  if (status != HAL_OK)
  {
    return status;
  }

  if ((verify & MCP23S17_IOCON_HAEN) != MCP23S17_IOCON_HAEN)
  {
    return HAL_ERROR;
  }

  /* 保守的上电状态：16 个扩展引脚全部配置为高阻输入。 */
  return MCP23S17_ConfigureGPIO(MCP23S17_PIN_ALL, 0U, 0U);
}

HAL_StatusTypeDef MCP23S17_WriteReg(uint8_t reg, uint8_t value)
{
  uint8_t tx_data[3];
  HAL_StatusTypeDef status;

  if (mcp23s17_hspi == NULL)
  {
    return HAL_ERROR;
  }

  tx_data[0] = MCP23S17_Opcode(0U);
  tx_data[1] = reg;
  tx_data[2] = value;

  /* 一次完整写命令必须被包含在同一个 CS 低电平窗口内。 */
  MCP23S17_Select();
  status = HAL_SPI_Transmit(mcp23s17_hspi, tx_data, sizeof(tx_data), MCP23S17_SPI_TIMEOUT_MS);
  MCP23S17_Deselect();

  return status;
}

HAL_StatusTypeDef MCP23S17_ReadReg(uint8_t reg, uint8_t *value)
{
  uint8_t tx_data[3];
  uint8_t rx_data[3];
  HAL_StatusTypeDef status;

  if ((mcp23s17_hspi == NULL) || (value == NULL))
  {
    return HAL_ERROR;
  }

  tx_data[0] = MCP23S17_Opcode(1U);
  tx_data[1] = reg;
  tx_data[2] = 0U;

  /*
   * 第三个字节是 dummy 发送字节；MCP23S17 会在该字节传输期间，
   * 通过 MISO 同步返回寄存器数据。
   */
  MCP23S17_Select();
  status = HAL_SPI_TransmitReceive(mcp23s17_hspi, tx_data, rx_data, sizeof(tx_data),
                                   MCP23S17_SPI_TIMEOUT_MS);
  MCP23S17_Deselect();

  if (status == HAL_OK)
  {
    *value = rx_data[2];
  }

  return status;
}

HAL_StatusTypeDef MCP23S17_WriteReg16(uint8_t reg_lsb, uint16_t value)
{
  HAL_StatusTypeDef status;

  /*
   * BANK=0 模式下 A/B 两组端口寄存器相邻。低字节对应 A 端口，
   * 高字节对应 B 端口。
   */
  status = MCP23S17_WriteReg(reg_lsb, (uint8_t)(value & 0x00FFU));
  if (status != HAL_OK)
  {
    return status;
  }

  return MCP23S17_WriteReg((uint8_t)(reg_lsb + 1U), (uint8_t)((value >> 8) & 0x00FFU));
}

HAL_StatusTypeDef MCP23S17_ReadReg16(uint8_t reg_lsb, uint16_t *value)
{
  uint8_t low = 0U;
  uint8_t high = 0U;
  HAL_StatusTypeDef status;

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  status = MCP23S17_ReadReg(reg_lsb, &low);
  if (status != HAL_OK)
  {
    return status;
  }

  status = MCP23S17_ReadReg((uint8_t)(reg_lsb + 1U), &high);
  if (status != HAL_OK)
  {
    return status;
  }

  *value = (uint16_t)low | ((uint16_t)high << 8);
  return HAL_OK;
}

HAL_StatusTypeDef MCP23S17_ConfigureGPIO(uint16_t input_mask, uint16_t pullup_mask, uint16_t output_value)
{
  HAL_StatusTypeDef status;

  /*
   * 先写输出锁存值，再修改方向寄存器。这样可以避免 IODIR 位切换为输出
   * 的瞬间，引脚短暂输出非预期电平。
   */
  mcp23s17_output_latch = output_value;

  status = MCP23S17_WriteReg16(MCP23S17_REG_OLATA, output_value);
  if (status != HAL_OK)
  {
    return status;
  }

  status = MCP23S17_WriteReg16(MCP23S17_REG_GPPUA, pullup_mask);
  if (status != HAL_OK)
  {
    return status;
  }

  return MCP23S17_WriteReg16(MCP23S17_REG_IODIRA, input_mask);
}

HAL_StatusTypeDef MCP23S17_ReadGPIO(uint16_t *value)
{
  /* GPIO 读取的是实际引脚电平，而不是单纯的输出锁存值。 */
  return MCP23S17_ReadReg16(MCP23S17_REG_GPIOA, value);
}

HAL_StatusTypeDef MCP23S17_WriteGPIO(uint16_t value)
{
  /* 保存影子副本，使按位 set/reset/toggle 接口无需先读取 OLAT。 */
  mcp23s17_output_latch = value;
  return MCP23S17_WriteReg16(MCP23S17_REG_OLATA, mcp23s17_output_latch);
}

HAL_StatusTypeDef MCP23S17_SetPins(uint16_t pin_mask)
{
  mcp23s17_output_latch |= pin_mask;
  return MCP23S17_WriteReg16(MCP23S17_REG_OLATA, mcp23s17_output_latch);
}

HAL_StatusTypeDef MCP23S17_ResetPins(uint16_t pin_mask)
{
  mcp23s17_output_latch &= (uint16_t)(~pin_mask);
  return MCP23S17_WriteReg16(MCP23S17_REG_OLATA, mcp23s17_output_latch);
}

HAL_StatusTypeDef MCP23S17_TogglePins(uint16_t pin_mask)
{
  mcp23s17_output_latch ^= pin_mask;
  return MCP23S17_WriteReg16(MCP23S17_REG_OLATA, mcp23s17_output_latch);
}
