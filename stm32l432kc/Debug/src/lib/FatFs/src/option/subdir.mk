################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/lib/FatFs/src/option/syscall.c 

OBJS += \
./src/lib/FatFs/src/option/syscall.o 

C_DEPS += \
./src/lib/FatFs/src/option/syscall.d 


# Each subdirectory must supply rules for building sources it contributes
src/lib/FatFs/src/option/%.o src/lib/FatFs/src/option/%.su src/lib/FatFs/src/option/%.cyclo: ../src/lib/FatFs/src/option/%.c src/lib/FatFs/src/option/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DSTM32L432xx -c -I"C:/project/LIGHT_ON/stm32l432kc/src" -I"C:/project/LIGHT_ON/stm32l432kc/src/ap" -I"C:/project/LIGHT_ON/stm32l432kc/src/bsp" -I"C:/project/LIGHT_ON/stm32l432kc/src/common" -I"C:/project/LIGHT_ON/stm32l432kc/src/common/core" -I"C:/project/LIGHT_ON/stm32l432kc/src/common/hw/include" -I"C:/project/LIGHT_ON/stm32l432kc/src/hw" -I"C:/project/LIGHT_ON/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Include" -I"C:/project/LIGHT_ON/stm32l432kc/src/lib/cube_l432/Drivers/STM32L4xx_HAL_Driver/Inc" -I"C:/project/LIGHT_ON/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Device/ST/STM32L4xx/Include" -I"C:/project/LIGHT_ON/stm32l432kc/src/lib/FatFs/src" -I"C:/project/LIGHT_ON/stm32l432kc/src/hw/driver/fatfs" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-src-2f-lib-2f-FatFs-2f-src-2f-option

clean-src-2f-lib-2f-FatFs-2f-src-2f-option:
	-$(RM) ./src/lib/FatFs/src/option/syscall.cyclo ./src/lib/FatFs/src/option/syscall.d ./src/lib/FatFs/src/option/syscall.o ./src/lib/FatFs/src/option/syscall.su

.PHONY: clean-src-2f-lib-2f-FatFs-2f-src-2f-option

