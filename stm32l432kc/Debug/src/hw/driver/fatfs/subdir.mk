################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/hw/driver/fatfs/sd_diskio.c 

OBJS += \
./src/hw/driver/fatfs/sd_diskio.o 

C_DEPS += \
./src/hw/driver/fatfs/sd_diskio.d 


# Each subdirectory must supply rules for building sources it contributes
src/hw/driver/fatfs/%.o src/hw/driver/fatfs/%.su src/hw/driver/fatfs/%.cyclo: ../src/hw/driver/fatfs/%.c src/hw/driver/fatfs/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DSTM32L432xx -c -I"C:/project/STM32L432KC/stm32l432kc/src" -I"C:/project/STM32L432KC/stm32l432kc/src/ap" -I"C:/project/STM32L432KC/stm32l432kc/src/bsp" -I"C:/project/STM32L432KC/stm32l432kc/src/common" -I"C:/project/STM32L432KC/stm32l432kc/src/common/core" -I"C:/project/STM32L432KC/stm32l432kc/src/common/hw/include" -I"C:/project/STM32L432KC/stm32l432kc/src/hw" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/STM32L4xx_HAL_Driver/Inc" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Device/ST/STM32L4xx/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/FatFs/src" -I"C:/project/STM32L432KC/stm32l432kc/src/hw/driver/fatfs" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-src-2f-hw-2f-driver-2f-fatfs

clean-src-2f-hw-2f-driver-2f-fatfs:
	-$(RM) ./src/hw/driver/fatfs/sd_diskio.cyclo ./src/hw/driver/fatfs/sd_diskio.d ./src/hw/driver/fatfs/sd_diskio.o ./src/hw/driver/fatfs/sd_diskio.su

.PHONY: clean-src-2f-hw-2f-driver-2f-fatfs

