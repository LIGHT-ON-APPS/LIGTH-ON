################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../TouchGFX/App/app_touchgfx.c 

OBJS += \
./TouchGFX/App/app_touchgfx.o 

C_DEPS += \
./TouchGFX/App/app_touchgfx.d 


# Each subdirectory must supply rules for building sources it contributes
TouchGFX/App/%.o TouchGFX/App/%.su TouchGFX/App/%.cyclo: ../TouchGFX/App/%.c TouchGFX/App/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DSTM32L432xx -c -I"C:/project/STM32L432KC/stm32l432kc/src" -I"C:/project/STM32L432KC/stm32l432kc/src/ap" -I"C:/project/STM32L432KC/stm32l432kc/src/bsp" -I"C:/project/STM32L432KC/stm32l432kc/src/common" -I"C:/project/STM32L432KC/stm32l432kc/src/common/core" -I"C:/project/STM32L432KC/stm32l432kc/src/common/hw/include" -I"C:/project/STM32L432KC/stm32l432kc/src/hw" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/STM32L4xx_HAL_Driver/Inc" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Device/ST/STM32L4xx/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/FatFs/src" -I"C:/project/STM32L432KC/stm32l432kc/src/hw/driver/fatfs" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-TouchGFX-2f-App

clean-TouchGFX-2f-App:
	-$(RM) ./TouchGFX/App/app_touchgfx.cyclo ./TouchGFX/App/app_touchgfx.d ./TouchGFX/App/app_touchgfx.o ./TouchGFX/App/app_touchgfx.su

.PHONY: clean-TouchGFX-2f-App

