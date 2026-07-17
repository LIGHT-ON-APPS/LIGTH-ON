################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/CacheableContainer.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Container.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ListLayout.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ModalWindow.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ScrollableContainer.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SlideMenu.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Slider.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SwipeContainer.cpp \
../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ZoomAnimationImage.cpp 

OBJS += \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/CacheableContainer.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Container.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ListLayout.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ModalWindow.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ScrollableContainer.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SlideMenu.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Slider.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SwipeContainer.o \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ZoomAnimationImage.o 

CPP_DEPS += \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/CacheableContainer.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Container.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ListLayout.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ModalWindow.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ScrollableContainer.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SlideMenu.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Slider.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SwipeContainer.d \
./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ZoomAnimationImage.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/ST/touchgfx/framework/source/touchgfx/containers/%.o Middlewares/ST/touchgfx/framework/source/touchgfx/containers/%.su Middlewares/ST/touchgfx/framework/source/touchgfx/containers/%.cyclo: ../Middlewares/ST/touchgfx/framework/source/touchgfx/containers/%.cpp Middlewares/ST/touchgfx/framework/source/touchgfx/containers/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++14 -g3 -DSTM32F411xE -c -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/ap" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/bsp" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common/core" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/common/hw/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Device/ST/STM32F4xx/Include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/lib/cube_f411/Drivers/STM32F4xx_HAL_Driver/Inc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/core" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/usb_cdc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/src/hw/driver/usb/usb_msc" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/App" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/target" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/target/generated" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/fonts/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/gui_generated/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/images/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/texts/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/generated/videos/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/TouchGFX/gui/include" -I"C:/project/LIGHTON_LCD/STM32F411/stm32f411_fw/Middlewares/ST/touchgfx/framework/include" -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middlewares-2f-ST-2f-touchgfx-2f-framework-2f-source-2f-touchgfx-2f-containers

clean-Middlewares-2f-ST-2f-touchgfx-2f-framework-2f-source-2f-touchgfx-2f-containers:
	-$(RM) ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/CacheableContainer.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/CacheableContainer.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/CacheableContainer.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/CacheableContainer.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Container.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Container.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Container.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Container.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ListLayout.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ListLayout.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ListLayout.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ListLayout.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ModalWindow.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ModalWindow.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ModalWindow.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ModalWindow.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ScrollableContainer.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ScrollableContainer.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ScrollableContainer.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ScrollableContainer.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SlideMenu.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SlideMenu.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SlideMenu.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SlideMenu.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Slider.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Slider.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Slider.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/Slider.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SwipeContainer.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SwipeContainer.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SwipeContainer.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/SwipeContainer.su ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ZoomAnimationImage.cyclo ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ZoomAnimationImage.d ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ZoomAnimationImage.o ./Middlewares/ST/touchgfx/framework/source/touchgfx/containers/ZoomAnimationImage.su

.PHONY: clean-Middlewares-2f-ST-2f-touchgfx-2f-framework-2f-source-2f-touchgfx-2f-containers

