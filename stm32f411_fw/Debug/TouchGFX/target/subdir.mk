################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../TouchGFX/target/STM32TouchController.cpp \
../TouchGFX/target/TouchGFXDataReader.cpp \
../TouchGFX/target/TouchGFXGPIO.cpp \
../TouchGFX/target/TouchGFXHAL.cpp 

OBJS += \
./TouchGFX/target/STM32TouchController.o \
./TouchGFX/target/TouchGFXDataReader.o \
./TouchGFX/target/TouchGFXGPIO.o \
./TouchGFX/target/TouchGFXHAL.o 

CPP_DEPS += \
./TouchGFX/target/STM32TouchController.d \
./TouchGFX/target/TouchGFXDataReader.d \
./TouchGFX/target/TouchGFXGPIO.d \
./TouchGFX/target/TouchGFXHAL.d 


# Each subdirectory must supply rules for building sources it contributes
TouchGFX/target/%.o TouchGFX/target/%.su TouchGFX/target/%.cyclo: ../TouchGFX/target/%.cpp TouchGFX/target/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++14 -g3 -DSTM32F411xE -c -I"C:/project/LIGHT_ON/stm32f411_fw/src" -I"C:/project/LIGHT_ON/stm32f411_fw/src/ap" -I"C:/project/LIGHT_ON/stm32f411_fw/src/bsp" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common/core" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common/hw/include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Device/ST/STM32F4xx/Include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/STM32F4xx_HAL_Driver/Inc" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/core" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/usb_cdc" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/usb_msc" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/App" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/target" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/target/generated" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/fonts/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/gui_generated/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/images/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/texts/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/videos/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/gui/include" -I"C:/project/LIGHT_ON/stm32f411_fw/Middlewares/ST/touchgfx/framework/include" -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-TouchGFX-2f-target

clean-TouchGFX-2f-target:
	-$(RM) ./TouchGFX/target/STM32TouchController.cyclo ./TouchGFX/target/STM32TouchController.d ./TouchGFX/target/STM32TouchController.o ./TouchGFX/target/STM32TouchController.su ./TouchGFX/target/TouchGFXDataReader.cyclo ./TouchGFX/target/TouchGFXDataReader.d ./TouchGFX/target/TouchGFXDataReader.o ./TouchGFX/target/TouchGFXDataReader.su ./TouchGFX/target/TouchGFXGPIO.cyclo ./TouchGFX/target/TouchGFXGPIO.d ./TouchGFX/target/TouchGFXGPIO.o ./TouchGFX/target/TouchGFXGPIO.su ./TouchGFX/target/TouchGFXHAL.cyclo ./TouchGFX/target/TouchGFXHAL.d ./TouchGFX/target/TouchGFXHAL.o ./TouchGFX/target/TouchGFXHAL.su

.PHONY: clean-TouchGFX-2f-target

