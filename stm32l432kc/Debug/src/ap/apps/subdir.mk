################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/ap/apps/apps.c \
../src/ap/apps/bamocar_can.c 

OBJS += \
./src/ap/apps/apps.o \
./src/ap/apps/bamocar_can.o 

C_DEPS += \
./src/ap/apps/apps.d \
./src/ap/apps/bamocar_can.d 


# Each subdirectory must supply rules for building sources it contributes
src/ap/apps/%.o src/ap/apps/%.su src/ap/apps/%.cyclo: ../src/ap/apps/%.c src/ap/apps/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DSTM32L432xx -c -I"C:/project/STM32L432KC/stm32l432kc/src" -I"C:/project/STM32L432KC/stm32l432kc/src/ap" -I"C:/project/STM32L432KC/stm32l432kc/src/bsp" -I"C:/project/STM32L432KC/stm32l432kc/src/common" -I"C:/project/STM32L432KC/stm32l432kc/src/common/core" -I"C:/project/STM32L432KC/stm32l432kc/src/common/hw/include" -I"C:/project/STM32L432KC/stm32l432kc/src/hw" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/STM32L4xx_HAL_Driver/Inc" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Device/ST/STM32L4xx/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/FatFs/src" -I"C:/project/STM32L432KC/stm32l432kc/src/hw/driver/fatfs" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-src-2f-ap-2f-apps

clean-src-2f-ap-2f-apps:
	-$(RM) ./src/ap/apps/apps.cyclo ./src/ap/apps/apps.d ./src/ap/apps/apps.o ./src/ap/apps/apps.su ./src/ap/apps/bamocar_can.cyclo ./src/ap/apps/bamocar_can.d ./src/ap/apps/bamocar_can.o ./src/ap/apps/bamocar_can.su

.PHONY: clean-src-2f-ap-2f-apps

