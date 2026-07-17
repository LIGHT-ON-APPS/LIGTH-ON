################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/lib/FatFs/src/diskio.c \
../src/lib/FatFs/src/ff.c \
../src/lib/FatFs/src/ff_gen_drv.c \
../src/lib/FatFs/src/ffsystem.c \
../src/lib/FatFs/src/ffunicode.c 

OBJS += \
./src/lib/FatFs/src/diskio.o \
./src/lib/FatFs/src/ff.o \
./src/lib/FatFs/src/ff_gen_drv.o \
./src/lib/FatFs/src/ffsystem.o \
./src/lib/FatFs/src/ffunicode.o 

C_DEPS += \
./src/lib/FatFs/src/diskio.d \
./src/lib/FatFs/src/ff.d \
./src/lib/FatFs/src/ff_gen_drv.d \
./src/lib/FatFs/src/ffsystem.d \
./src/lib/FatFs/src/ffunicode.d 


# Each subdirectory must supply rules for building sources it contributes
src/lib/FatFs/src/%.o src/lib/FatFs/src/%.su src/lib/FatFs/src/%.cyclo: ../src/lib/FatFs/src/%.c src/lib/FatFs/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DSTM32L432xx -c -I"C:/project/STM32L432KC/stm32l432kc/src" -I"C:/project/STM32L432KC/stm32l432kc/src/ap" -I"C:/project/STM32L432KC/stm32l432kc/src/bsp" -I"C:/project/STM32L432KC/stm32l432kc/src/common" -I"C:/project/STM32L432KC/stm32l432kc/src/common/core" -I"C:/project/STM32L432KC/stm32l432kc/src/common/hw/include" -I"C:/project/STM32L432KC/stm32l432kc/src/hw" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/STM32L4xx_HAL_Driver/Inc" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Device/ST/STM32L4xx/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/FatFs/src" -I"C:/project/STM32L432KC/stm32l432kc/src/hw/driver/fatfs" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-src-2f-lib-2f-FatFs-2f-src

clean-src-2f-lib-2f-FatFs-2f-src:
	-$(RM) ./src/lib/FatFs/src/diskio.cyclo ./src/lib/FatFs/src/diskio.d ./src/lib/FatFs/src/diskio.o ./src/lib/FatFs/src/diskio.su ./src/lib/FatFs/src/ff.cyclo ./src/lib/FatFs/src/ff.d ./src/lib/FatFs/src/ff.o ./src/lib/FatFs/src/ff.su ./src/lib/FatFs/src/ff_gen_drv.cyclo ./src/lib/FatFs/src/ff_gen_drv.d ./src/lib/FatFs/src/ff_gen_drv.o ./src/lib/FatFs/src/ff_gen_drv.su ./src/lib/FatFs/src/ffsystem.cyclo ./src/lib/FatFs/src/ffsystem.d ./src/lib/FatFs/src/ffsystem.o ./src/lib/FatFs/src/ffsystem.su ./src/lib/FatFs/src/ffunicode.cyclo ./src/lib/FatFs/src/ffunicode.d ./src/lib/FatFs/src/ffunicode.o ./src/lib/FatFs/src/ffunicode.su

.PHONY: clean-src-2f-lib-2f-FatFs-2f-src

