/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : STM32TouchController.cpp
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* USER CODE BEGIN STM32TouchController */

#include <STM32TouchController.hpp>
#include "main.h"
#include "utility/utility.h"

extern "C" I2C_HandleTypeDef hi2c1;
// Optional: Add UART for debug
//extern "C" UART_HandleTypeDef huart1;

void STM32TouchController::init()
{
  /**
   * Initialize touch controller and driver for CF1124
   */
  TP.type = CTP;
  TP.driver = unknown;
  TP.range.x = 320;
  TP.range.y = 480;
  TP.version = 0;

  // Reset sequence for CF1124 (RESET pin low ?��1µs, hold low 5ms after VDD stable)
  HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_RESET); // Set reset pin low
  HAL_Delay(1); // Reduced to 1ms (?��1µs required)
  HAL_GPIO_WritePin(CTP_RST_GPIO_Port, CTP_RST_Pin, GPIO_PIN_SET); // Set reset pin high
  HAL_Delay(5); // Reduced to 5ms (typical for Sitronix post-reset stabilization)

  getDriver();
  getVersion();
}

bool STM32TouchController::sampleTouch(int32_t& x, int32_t& y)
{
  return CF1124_Read(x, y);
}

void STM32TouchController::getDriver(void)
{
  uint8_t Buf[1];
  TP.driver = unknown;

  // Read firmware version register (0x00, per Sitronix protocol)
  if (HAL_I2C_Mem_Read( &hi2c1, 0x55 << 1, 0x00, I2C_MEMADD_SIZE_8BIT, Buf, 1, 10 ) == HAL_OK) {

    // CF1124: Assume any non-zero firmware version indicates presence
    if (Buf[0] != 0x00 && Buf[0] != 0xFF) {
        TP.driver = CF1124;
    }
  }
}

void STM32TouchController::getVersion(void)
{
  uint8_t Buf[1];

  switch (TP.driver) {

    case CF1124:

      // Read firmware version (0x00, per Sitronix protocol)
      if (HAL_I2C_Mem_Read(&hi2c1, 0x55 << 1, 0x00, I2C_MEMADD_SIZE_8BIT, Buf, 1, 10) == HAL_OK) {

        if (Buf[0] > 0 && Buf[0] < 255) {
          TP.version = Buf[0];
        }
      }

      break;

    default:
      return;
  }
}

bool STM32TouchController::CF1124_Read(int32_t& x, int32_t& y)
{
  uint8_t Buf[5]; // Read 5 bytes (status + 4 bytes for single touch: high byte, X low, Y high, Y low)

  // Read touch data from register 0x10 (based on CF1124_Read_Touch)
  if ( HAL_I2C_Mem_Read(&hi2c1, 0x55 << 1, 0x10, I2C_MEMADD_SIZE_8BIT, Buf, 5, 10) != HAL_OK ) {

    return false;
  }

  // Check touch status (bit 7 of status byte 2)
  if ( !( Buf[2] & 0x80 ) ) {

    return false;
  }

  // Extract X coordinate: bits 6-4 of byte 2 (X_H) << 4 | byte 3 (X_L)
  uint32_t sx = ((Buf[2] & 0x70) << 4) | Buf[3];

  // Extract Y coordinate: bits 2-0 of byte 2 (Y_H) << 8 | byte 4 (Y_L)
  uint32_t sy = ((Buf[2] & 0x07) << 8) | Buf[4];

  // Printf( "sx %d, sy %d \n", sx, sy );

  x = sx;
  y = sy;

  // CF1124 uses 12-bit resolution (0-4095); scale to display (320x480)
  // x = (sx * 320) / 4096;
  // y = (sy * 480) / 4096;
  // Printf("x and y %d, %d.\n \n", x, y );
  return true;
}

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
