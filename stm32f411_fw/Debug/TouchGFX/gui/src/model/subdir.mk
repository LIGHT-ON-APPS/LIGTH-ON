################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../TouchGFX/gui/src/model/Model.cpp 

OBJS += \
./TouchGFX/gui/src/model/Model.o 

CPP_DEPS += \
./TouchGFX/gui/src/model/Model.d 


# Each subdirectory must supply rules for building sources it contributes
TouchGFX/gui/src/model/%.o TouchGFX/gui/src/model/%.su TouchGFX/gui/src/model/%.cyclo: ../TouchGFX/gui/src/model/%.cpp TouchGFX/gui/src/model/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++14 -g3 -DSTM32F411xE -c -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/ap" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/bsp" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common/core" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common/hw/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Device/ST/STM32F4xx/Include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/STM32F4xx_HAL_Driver/Inc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/core" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/usb_cdc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/usb_msc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/App" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/target" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/target/generated" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/fonts/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/gui_generated/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/images/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/texts/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/videos/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/gui/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/Middlewares/ST/touchgfx/framework/include" -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-TouchGFX-2f-gui-2f-src-2f-model

clean-TouchGFX-2f-gui-2f-src-2f-model:
	-$(RM) ./TouchGFX/gui/src/model/Model.cyclo ./TouchGFX/gui/src/model/Model.d ./TouchGFX/gui/src/model/Model.o ./TouchGFX/gui/src/model/Model.su

.PHONY: clean-TouchGFX-2f-gui-2f-src-2f-model

