################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/hw/driver/cdc.c \
../src/hw/driver/cli.c \
../src/hw/driver/gpio.c \
../src/hw/driver/lcd.c \
../src/hw/driver/led.c \
../src/hw/driver/sd.c \
../src/hw/driver/spi.c \
../src/hw/driver/uart.c \
../src/hw/driver/usb.c 

C_DEPS += \
./src/hw/driver/cdc.d \
./src/hw/driver/cli.d \
./src/hw/driver/gpio.d \
./src/hw/driver/lcd.d \
./src/hw/driver/led.d \
./src/hw/driver/sd.d \
./src/hw/driver/spi.d \
./src/hw/driver/uart.d \
./src/hw/driver/usb.d 

OBJS += \
./src/hw/driver/cdc.o \
./src/hw/driver/cli.o \
./src/hw/driver/gpio.o \
./src/hw/driver/lcd.o \
./src/hw/driver/led.o \
./src/hw/driver/sd.o \
./src/hw/driver/spi.o \
./src/hw/driver/uart.o \
./src/hw/driver/usb.o 


# Each subdirectory must supply rules for building sources it contributes
src/hw/driver/%.o src/hw/driver/%.su src/hw/driver/%.cyclo: ../src/hw/driver/%.c src/hw/driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DSTM32F411xE -c -I"C:/project/LIGHT_ON/stm32f411_fw/src" -I"C:/project/LIGHT_ON/stm32f411_fw/src/ap" -I"C:/project/LIGHT_ON/stm32f411_fw/src/bsp" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common/core" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common/hw/include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Device/ST/STM32F4xx/Include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/STM32F4xx_HAL_Driver/Inc" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/core" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/usb_cdc" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/usb_msc" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-src-2f-hw-2f-driver

clean-src-2f-hw-2f-driver:
	-$(RM) ./src/hw/driver/cdc.cyclo ./src/hw/driver/cdc.d ./src/hw/driver/cdc.o ./src/hw/driver/cdc.su ./src/hw/driver/cli.cyclo ./src/hw/driver/cli.d ./src/hw/driver/cli.o ./src/hw/driver/cli.su ./src/hw/driver/gpio.cyclo ./src/hw/driver/gpio.d ./src/hw/driver/gpio.o ./src/hw/driver/gpio.su ./src/hw/driver/lcd.cyclo ./src/hw/driver/lcd.d ./src/hw/driver/lcd.o ./src/hw/driver/lcd.su ./src/hw/driver/led.cyclo ./src/hw/driver/led.d ./src/hw/driver/led.o ./src/hw/driver/led.su ./src/hw/driver/sd.cyclo ./src/hw/driver/sd.d ./src/hw/driver/sd.o ./src/hw/driver/sd.su ./src/hw/driver/spi.cyclo ./src/hw/driver/spi.d ./src/hw/driver/spi.o ./src/hw/driver/spi.su ./src/hw/driver/uart.cyclo ./src/hw/driver/uart.d ./src/hw/driver/uart.o ./src/hw/driver/uart.su ./src/hw/driver/usb.cyclo ./src/hw/driver/usb.d ./src/hw/driver/usb.o ./src/hw/driver/usb.su

.PHONY: clean-src-2f-hw-2f-driver

