/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : STM32TouchController.hpp
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

#ifndef STM32TOUCHCONTROLLER_HPP
#define STM32TOUCHCONTROLLER_HPP

#include <platform/driver/touch/TouchController.hpp>

/**
 * @class STM32TouchController
 *
 * @brief This class specializes TouchController Interface for CF1124 CTP Driver IC.
 *
 * @sa touchgfx::TouchController
 */
class STM32TouchController : public touchgfx::TouchController
{
public:
    STM32TouchController() {}

    /**
     * @brief Initializes touch controller.
     */
    virtual void init();

    /**
     * @brief Checks whether the touch screen is being touched, and if so, what coordinates.
     *
     * @param [out] x The x position of the touch
     * @param [out] y The y position of the touch
     *
     * @return True if a touch has been detected, otherwise false.
     */
    virtual bool sampleTouch(int32_t& x, int32_t& y);

private:
    typedef enum {
        none = 0x00,
        RTP,
        CTP,
    } tp_type;

    typedef enum {
        unknown = 0x00,
        CF1124,
    } tp_driver;

    struct {
        tp_type type;
        tp_driver driver;
        struct {
            uint16_t x;
            uint16_t y;
        } range;
        uint32_t version;
    } TP;

    void getDriver(void);
    void getVersion(void);
    bool CF1124_Read(int32_t& x, int32_t& y);
};

#endif // STM32TOUCHCONTROLLER_HPP

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
