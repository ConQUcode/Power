/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tm1637.c
  * @brief   TM1637 四位数码管显示驱动。
  *
  * TM1637 的通信流程：
  *   1. 发送数据命令 0x40，使用地址自动递增模式。
  *   2. 发送地址命令 0xC0 + position。
  *   3. 连续写入 1..4 字节显示 RAM。
  *   4. 发送显示控制命令 0x88 | brightness。
  *
  * DIO 线需要开漏或等效的释放高电平能力，以便读取 TM1637 的 ACK。
  * 当前 CubeMX 已将 TM1637_CLK/TM1637_DIO 配置为开漏输出并默认置高。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "tm1637.h"

#define TM1637_CMD_DATA_AUTO      (0x40U)
#define TM1637_CMD_ADDRESS_BASE   (0xC0U)
#define TM1637_CMD_DISPLAY_BASE   (0x80U)
#define TM1637_CMD_DISPLAY_ON     (0x08U)
#define TM1637_BRIGHTNESS_MASK    (0x07U)
#define TM1637_DEFAULT_DELAY_US   (3U)
#define TM1637_MAX_ACK_WAIT       (1000U)

static uint8_t tm1637_brightness = 7U;
static uint8_t tm1637_display_on = 1U;
static uint32_t tm1637_delay_us = TM1637_DEFAULT_DELAY_US;

static const uint8_t tm1637_digit_segments[10] =
{
  TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F,                /* 0 */
  TM1637_SEG_B | TM1637_SEG_C,                                                                            /* 1 */
  TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_G,                               /* 2 */
  TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_G,                               /* 3 */
  TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_F | TM1637_SEG_G,                                              /* 4 */
  TM1637_SEG_A | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_F | TM1637_SEG_G,                               /* 5 */
  TM1637_SEG_A | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G,                /* 6 */
  TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_C,                                                            /* 7 */
  TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G, /* 8 */
  TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_F | TM1637_SEG_G                 /* 9 */
};

static void TM1637_DelayUs(uint32_t delay_us)
{
  uint32_t start_tick;
  uint32_t wait_cycles;
  volatile uint32_t fallback_count;

  if (delay_us == 0U)
  {
    delay_us = 1U;
  }

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U)
  {
    start_tick = DWT->CYCCNT;
    wait_cycles = (SystemCoreClock / 1000000U) * delay_us;
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

static void TM1637_SetClk(GPIO_PinState state)
{
  HAL_GPIO_WritePin(TM1637_CLK_GPIO_Port, TM1637_CLK_Pin, state);
}

static void TM1637_SetDio(GPIO_PinState state)
{
  HAL_GPIO_WritePin(TM1637_DIO_GPIO_Port, TM1637_DIO_Pin, state);
}

static GPIO_PinState TM1637_ReadDio(void)
{
  return HAL_GPIO_ReadPin(TM1637_DIO_GPIO_Port, TM1637_DIO_Pin);
}

static void TM1637_Start(void)
{
  TM1637_SetDio(GPIO_PIN_SET);
  TM1637_SetClk(GPIO_PIN_SET);
  TM1637_DelayUs(tm1637_delay_us);
  TM1637_SetDio(GPIO_PIN_RESET);
  TM1637_DelayUs(tm1637_delay_us);
  TM1637_SetClk(GPIO_PIN_RESET);
}

static void TM1637_Stop(void)
{
  TM1637_SetClk(GPIO_PIN_RESET);
  TM1637_SetDio(GPIO_PIN_RESET);
  TM1637_DelayUs(tm1637_delay_us);
  TM1637_SetClk(GPIO_PIN_SET);
  TM1637_DelayUs(tm1637_delay_us);
  TM1637_SetDio(GPIO_PIN_SET);
  TM1637_DelayUs(tm1637_delay_us);
}

static HAL_StatusTypeDef TM1637_WriteByte(uint8_t value)
{
  uint8_t bit_index;
  uint32_t wait_count;
  HAL_StatusTypeDef status = HAL_OK;

  for (bit_index = 0U; bit_index < 8U; bit_index++)
  {
    TM1637_SetClk(GPIO_PIN_RESET);
    TM1637_SetDio(((value & 0x01U) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    TM1637_DelayUs(tm1637_delay_us);
    TM1637_SetClk(GPIO_PIN_SET);
    TM1637_DelayUs(tm1637_delay_us);
    value >>= 1U;
  }

  TM1637_SetClk(GPIO_PIN_RESET);
  TM1637_SetDio(GPIO_PIN_SET);
  TM1637_DelayUs(tm1637_delay_us);
  TM1637_SetClk(GPIO_PIN_SET);

  /*
   * DIO 为开漏输出时，写 1 等价于释放总线。TM1637 正常应答会把
   * DIO 拉低；如果线路异常或模块未连接，等待不会无限卡死。
   */
  wait_count = TM1637_MAX_ACK_WAIT;
  while ((TM1637_ReadDio() == GPIO_PIN_SET) && (wait_count > 0U))
  {
    wait_count--;
  }

  if (wait_count == 0U)
  {
    status = HAL_TIMEOUT;
  }

  TM1637_DelayUs(tm1637_delay_us);
  TM1637_SetClk(GPIO_PIN_RESET);
  return status;
}

static HAL_StatusTypeDef TM1637_WriteCommand(uint8_t command)
{
  HAL_StatusTypeDef status;

  TM1637_Start();
  status = TM1637_WriteByte(command);
  TM1637_Stop();

  return status;
}

static HAL_StatusTypeDef TM1637_ApplyDisplayControl(void)
{
  uint8_t command;

  command = TM1637_CMD_DISPLAY_BASE | (tm1637_brightness & TM1637_BRIGHTNESS_MASK);
  if (tm1637_display_on != 0U)
  {
    command |= TM1637_CMD_DISPLAY_ON;
  }

  return TM1637_WriteCommand(command);
}

HAL_StatusTypeDef TM1637_Init(void)
{
  tm1637_brightness = 7U;
  tm1637_display_on = 1U;

  TM1637_SetClk(GPIO_PIN_SET);
  TM1637_SetDio(GPIO_PIN_SET);
  TM1637_DelayUs(tm1637_delay_us);

  return TM1637_Clear();
}

void TM1637_SetDelayUs(uint32_t delay_us)
{
  tm1637_delay_us = (delay_us == 0U) ? 1U : delay_us;
}

HAL_StatusTypeDef TM1637_SetBrightness(uint8_t brightness, uint8_t display_on)
{
  tm1637_brightness = brightness & TM1637_BRIGHTNESS_MASK;
  tm1637_display_on = (display_on != 0U) ? 1U : 0U;

  return TM1637_ApplyDisplayControl();
}

HAL_StatusTypeDef TM1637_Clear(void)
{
  uint8_t blank[TM1637_DIGIT_COUNT] = {0U, 0U, 0U, 0U};

  return TM1637_DisplayRaw(0U, blank, TM1637_DIGIT_COUNT);
}

HAL_StatusTypeDef TM1637_DisplayRaw(uint8_t position, const uint8_t *segments, uint8_t length)
{
  uint8_t index;
  HAL_StatusTypeDef status;

  if ((segments == NULL) || (position >= TM1637_DIGIT_COUNT) || (length == 0U))
  {
    return HAL_ERROR;
  }

  if ((position + length) > TM1637_DIGIT_COUNT)
  {
    length = TM1637_DIGIT_COUNT - position;
  }

  status = TM1637_WriteCommand(TM1637_CMD_DATA_AUTO);
  if (status != HAL_OK)
  {
    return status;
  }

  TM1637_Start();
  status = TM1637_WriteByte((uint8_t)(TM1637_CMD_ADDRESS_BASE + position));
  for (index = 0U; (index < length) && (status == HAL_OK); index++)
  {
    status = TM1637_WriteByte(segments[index]);
  }
  TM1637_Stop();

  if (status != HAL_OK)
  {
    return status;
  }

  return TM1637_ApplyDisplayControl();
}

HAL_StatusTypeDef TM1637_DisplayText(const char *text, uint8_t length, uint8_t dot_mask)
{
  uint8_t segments[TM1637_DIGIT_COUNT] = {0U, 0U, 0U, 0U};
  uint8_t index;

  if (text == NULL)
  {
    return HAL_ERROR;
  }

  if (length > TM1637_DIGIT_COUNT)
  {
    length = TM1637_DIGIT_COUNT;
  }

  for (index = 0U; index < length; index++)
  {
    segments[index] = TM1637_EncodeChar(text[index]);
    if ((dot_mask & (1U << index)) != 0U)
    {
      segments[index] |= TM1637_SEG_DOT;
    }
  }

  return TM1637_DisplayRaw(0U, segments, TM1637_DIGIT_COUNT);
}

HAL_StatusTypeDef TM1637_DisplayNumber(int16_t value, uint8_t show_leading_zero, uint8_t dot_mask)
{
  uint8_t segments[TM1637_DIGIT_COUNT] = {0U, 0U, 0U, 0U};
  uint16_t number;
  uint8_t position;
  uint8_t negative = 0U;

  if ((value > 9999) || (value < -999))
  {
    segments[0] = TM1637_SEG_G;
    segments[1] = TM1637_SEG_G;
    segments[2] = TM1637_SEG_G;
    segments[3] = TM1637_SEG_G;
  }
  else
  {
    if (value < 0)
    {
      negative = 1U;
      number = (uint16_t)(-value);
    }
    else
    {
      number = (uint16_t)value;
    }

    for (position = TM1637_DIGIT_COUNT; position > 0U; position--)
    {
      segments[position - 1U] = TM1637_EncodeDigit((uint8_t)(number % 10U));
      number /= 10U;

      if ((number == 0U) && (position > 1U) && (show_leading_zero == 0U))
      {
        break;
      }
    }

    if (negative != 0U)
    {
      if (position > 1U)
      {
        segments[position - 2U] = TM1637_SEG_G;
      }
      else
      {
        segments[0] = TM1637_SEG_G;
      }
    }
  }

  for (position = 0U; position < TM1637_DIGIT_COUNT; position++)
  {
    if ((dot_mask & (1U << position)) != 0U)
    {
      segments[position] |= TM1637_SEG_DOT;
    }
  }

  return TM1637_DisplayRaw(0U, segments, TM1637_DIGIT_COUNT);
}

uint8_t TM1637_EncodeDigit(uint8_t digit)
{
  if (digit < 10U)
  {
    return tm1637_digit_segments[digit];
  }

  return 0U;
}

uint8_t TM1637_EncodeChar(char ch)
{
  if ((ch >= '0') && (ch <= '9'))
  {
    return TM1637_EncodeDigit((uint8_t)(ch - '0'));
  }

  switch (ch)
  {
    case 'A':
    case 'a':
      return TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G;

    case 'B':
    case 'b':
      return TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G;

    case 'C':
    case 'c':
      return TM1637_SEG_A | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F;

    case 'D':
    case 'd':
      return TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_G;

    case 'E':
    case 'e':
      return TM1637_SEG_A | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G;

    case 'F':
    case 'f':
      return TM1637_SEG_A | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G;

    case 'H':
    case 'h':
      return TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G;

    case 'L':
    case 'l':
      return TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F;

    case 'O':
    case 'o':
      return TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_G;

    case 'P':
    case 'p':
      return TM1637_SEG_A | TM1637_SEG_B | TM1637_SEG_E | TM1637_SEG_F | TM1637_SEG_G;

    case 'U':
    case 'u':
      return TM1637_SEG_B | TM1637_SEG_C | TM1637_SEG_D | TM1637_SEG_E | TM1637_SEG_F;

    case '-':
      return TM1637_SEG_G;

    case '_':
      return TM1637_SEG_D;

    case ' ':
    default:
      return 0U;
  }
}
