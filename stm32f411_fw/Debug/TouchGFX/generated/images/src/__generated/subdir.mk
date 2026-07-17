################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../TouchGFX/generated/images/src/__generated/image_alternate_theme_images_widgets_circleprogress_backgrounds_medium.cpp 

OBJS += \
./TouchGFX/generated/images/src/__generated/image_alternate_theme_images_widgets_circleprogress_backgrounds_medium.o 

CPP_DEPS += \
./TouchGFX/generated/images/src/__generated/image_alternate_theme_images_widgets_circleprogress_backgrounds_medium.d 


# Each subdirectory must supply rules for building sources it contributes
TouchGFX/generated/images/src/__generated/%.o TouchGFX/generated/images/src/__generated/%.su TouchGFX/generated/images/src/__generated/%.cyclo: ../TouchGFX/generated/images/src/__generated/%.cpp TouchGFX/generated/images/src/__generated/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++14 -g3 -DSTM32F411xE -c -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/ap" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/bsp" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common/core" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common/hw/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Device/ST/STM32F4xx/Include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/STM32F4xx_HAL_Driver/Inc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/core" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/usb_cdc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/usb_msc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/App" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/target" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/target/generated" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/fonts/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/gui_generated/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/images/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/texts/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/videos/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/gui/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/Middlewares/ST/touchgfx/framework/include" -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-TouchGFX-2f-generated-2f-images-2f-src-2f-__generated

clean-TouchGFX-2f-generated-2f-images-2f-src-2f-__generated:
	-$(RM) ./TouchGFX/generated/images/src/__generated/image_alternate_theme_images_widgets_circleprogress_backgrounds_medium.cyclo ./TouchGFX/generated/images/src/__generated/image_alternate_theme_images_widgets_circleprogress_backgrounds_medium.d ./TouchGFX/generated/images/src/__generated/image_alternate_theme_images_widgets_circleprogress_backgrounds_medium.o ./TouchGFX/generated/images/src/__generated/image_alternate_theme_images_widgets_circleprogress_backgrounds_medium.su

.PHONY: clean-TouchGFX-2f-generated-2f-images-2f-src-2f-__generated

