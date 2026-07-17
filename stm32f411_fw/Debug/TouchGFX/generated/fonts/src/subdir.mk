################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../TouchGFX/generated/fonts/src/ApplicationFontProvider.cpp \
../TouchGFX/generated/fonts/src/CachedFont.cpp \
../TouchGFX/generated/fonts/src/CompressedFontCache.cpp \
../TouchGFX/generated/fonts/src/CompressedUnmappedFontCache.cpp \
../TouchGFX/generated/fonts/src/FontCache.cpp \
../TouchGFX/generated/fonts/src/Font_verdana_10_4bpp_0.cpp \
../TouchGFX/generated/fonts/src/Font_verdana_13_4bpp_0.cpp \
../TouchGFX/generated/fonts/src/Font_verdana_16_4bpp_0.cpp \
../TouchGFX/generated/fonts/src/Font_verdana_20_4bpp_0.cpp \
../TouchGFX/generated/fonts/src/Font_verdana_40_4bpp_0.cpp \
../TouchGFX/generated/fonts/src/GeneratedFont.cpp \
../TouchGFX/generated/fonts/src/Kerning_verdana_10_4bpp.cpp \
../TouchGFX/generated/fonts/src/Kerning_verdana_13_4bpp.cpp \
../TouchGFX/generated/fonts/src/Kerning_verdana_16_4bpp.cpp \
../TouchGFX/generated/fonts/src/Kerning_verdana_20_4bpp.cpp \
../TouchGFX/generated/fonts/src/Kerning_verdana_40_4bpp.cpp \
../TouchGFX/generated/fonts/src/Table_verdana_10_4bpp.cpp \
../TouchGFX/generated/fonts/src/Table_verdana_13_4bpp.cpp \
../TouchGFX/generated/fonts/src/Table_verdana_16_4bpp.cpp \
../TouchGFX/generated/fonts/src/Table_verdana_20_4bpp.cpp \
../TouchGFX/generated/fonts/src/Table_verdana_40_4bpp.cpp \
../TouchGFX/generated/fonts/src/UnmappedDataFont.cpp \
../TouchGFX/generated/fonts/src/VectorFontRendererBuffers.cpp 

OBJS += \
./TouchGFX/generated/fonts/src/ApplicationFontProvider.o \
./TouchGFX/generated/fonts/src/CachedFont.o \
./TouchGFX/generated/fonts/src/CompressedFontCache.o \
./TouchGFX/generated/fonts/src/CompressedUnmappedFontCache.o \
./TouchGFX/generated/fonts/src/FontCache.o \
./TouchGFX/generated/fonts/src/Font_verdana_10_4bpp_0.o \
./TouchGFX/generated/fonts/src/Font_verdana_13_4bpp_0.o \
./TouchGFX/generated/fonts/src/Font_verdana_16_4bpp_0.o \
./TouchGFX/generated/fonts/src/Font_verdana_20_4bpp_0.o \
./TouchGFX/generated/fonts/src/Font_verdana_40_4bpp_0.o \
./TouchGFX/generated/fonts/src/GeneratedFont.o \
./TouchGFX/generated/fonts/src/Kerning_verdana_10_4bpp.o \
./TouchGFX/generated/fonts/src/Kerning_verdana_13_4bpp.o \
./TouchGFX/generated/fonts/src/Kerning_verdana_16_4bpp.o \
./TouchGFX/generated/fonts/src/Kerning_verdana_20_4bpp.o \
./TouchGFX/generated/fonts/src/Kerning_verdana_40_4bpp.o \
./TouchGFX/generated/fonts/src/Table_verdana_10_4bpp.o \
./TouchGFX/generated/fonts/src/Table_verdana_13_4bpp.o \
./TouchGFX/generated/fonts/src/Table_verdana_16_4bpp.o \
./TouchGFX/generated/fonts/src/Table_verdana_20_4bpp.o \
./TouchGFX/generated/fonts/src/Table_verdana_40_4bpp.o \
./TouchGFX/generated/fonts/src/UnmappedDataFont.o \
./TouchGFX/generated/fonts/src/VectorFontRendererBuffers.o 

CPP_DEPS += \
./TouchGFX/generated/fonts/src/ApplicationFontProvider.d \
./TouchGFX/generated/fonts/src/CachedFont.d \
./TouchGFX/generated/fonts/src/CompressedFontCache.d \
./TouchGFX/generated/fonts/src/CompressedUnmappedFontCache.d \
./TouchGFX/generated/fonts/src/FontCache.d \
./TouchGFX/generated/fonts/src/Font_verdana_10_4bpp_0.d \
./TouchGFX/generated/fonts/src/Font_verdana_13_4bpp_0.d \
./TouchGFX/generated/fonts/src/Font_verdana_16_4bpp_0.d \
./TouchGFX/generated/fonts/src/Font_verdana_20_4bpp_0.d \
./TouchGFX/generated/fonts/src/Font_verdana_40_4bpp_0.d \
./TouchGFX/generated/fonts/src/GeneratedFont.d \
./TouchGFX/generated/fonts/src/Kerning_verdana_10_4bpp.d \
./TouchGFX/generated/fonts/src/Kerning_verdana_13_4bpp.d \
./TouchGFX/generated/fonts/src/Kerning_verdana_16_4bpp.d \
./TouchGFX/generated/fonts/src/Kerning_verdana_20_4bpp.d \
./TouchGFX/generated/fonts/src/Kerning_verdana_40_4bpp.d \
./TouchGFX/generated/fonts/src/Table_verdana_10_4bpp.d \
./TouchGFX/generated/fonts/src/Table_verdana_13_4bpp.d \
./TouchGFX/generated/fonts/src/Table_verdana_16_4bpp.d \
./TouchGFX/generated/fonts/src/Table_verdana_20_4bpp.d \
./TouchGFX/generated/fonts/src/Table_verdana_40_4bpp.d \
./TouchGFX/generated/fonts/src/UnmappedDataFont.d \
./TouchGFX/generated/fonts/src/VectorFontRendererBuffers.d 


# Each subdirectory must supply rules for building sources it contributes
TouchGFX/generated/fonts/src/%.o TouchGFX/generated/fonts/src/%.su TouchGFX/generated/fonts/src/%.cyclo: ../TouchGFX/generated/fonts/src/%.cpp TouchGFX/generated/fonts/src/subdir.mk
	arm-none-eabi-g++ "$<" -mcpu=cortex-m4 -std=gnu++14 -g3 -DSTM32F411xE -c -I"C:/project/LIGHT_ON/stm32f411_fw/src" -I"C:/project/LIGHT_ON/stm32f411_fw/src/ap" -I"C:/project/LIGHT_ON/stm32f411_fw/src/bsp" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common/core" -I"C:/project/LIGHT_ON/stm32f411_fw/src/common/hw/include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Device/ST/STM32F4xx/Include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/CMSIS/Include" -I"C:/project/LIGHT_ON/stm32f411_fw/src/lib/cube_f411/Drivers/STM32F4xx_HAL_Driver/Inc" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/core" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/usb_cdc" -I"C:/project/LIGHT_ON/stm32f411_fw/src/hw/driver/usb/usb_msc" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/App" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/target" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/target/generated" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/fonts/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/gui_generated/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/images/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/texts/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/generated/videos/include" -I"C:/project/LIGHT_ON/stm32f411_fw/TouchGFX/gui/include" -I"C:/project/LIGHT_ON/stm32f411_fw/Middlewares/ST/touchgfx/framework/include" -O0 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-TouchGFX-2f-generated-2f-fonts-2f-src

clean-TouchGFX-2f-generated-2f-fonts-2f-src:
	-$(RM) ./TouchGFX/generated/fonts/src/ApplicationFontProvider.cyclo ./TouchGFX/generated/fonts/src/ApplicationFontProvider.d ./TouchGFX/generated/fonts/src/ApplicationFontProvider.o ./TouchGFX/generated/fonts/src/ApplicationFontProvider.su ./TouchGFX/generated/fonts/src/CachedFont.cyclo ./TouchGFX/generated/fonts/src/CachedFont.d ./TouchGFX/generated/fonts/src/CachedFont.o ./TouchGFX/generated/fonts/src/CachedFont.su ./TouchGFX/generated/fonts/src/CompressedFontCache.cyclo ./TouchGFX/generated/fonts/src/CompressedFontCache.d ./TouchGFX/generated/fonts/src/CompressedFontCache.o ./TouchGFX/generated/fonts/src/CompressedFontCache.su ./TouchGFX/generated/fonts/src/CompressedUnmappedFontCache.cyclo ./TouchGFX/generated/fonts/src/CompressedUnmappedFontCache.d ./TouchGFX/generated/fonts/src/CompressedUnmappedFontCache.o ./TouchGFX/generated/fonts/src/CompressedUnmappedFontCache.su ./TouchGFX/generated/fonts/src/FontCache.cyclo ./TouchGFX/generated/fonts/src/FontCache.d ./TouchGFX/generated/fonts/src/FontCache.o ./TouchGFX/generated/fonts/src/FontCache.su ./TouchGFX/generated/fonts/src/Font_verdana_10_4bpp_0.cyclo ./TouchGFX/generated/fonts/src/Font_verdana_10_4bpp_0.d ./TouchGFX/generated/fonts/src/Font_verdana_10_4bpp_0.o ./TouchGFX/generated/fonts/src/Font_verdana_10_4bpp_0.su ./TouchGFX/generated/fonts/src/Font_verdana_13_4bpp_0.cyclo ./TouchGFX/generated/fonts/src/Font_verdana_13_4bpp_0.d ./TouchGFX/generated/fonts/src/Font_verdana_13_4bpp_0.o ./TouchGFX/generated/fonts/src/Font_verdana_13_4bpp_0.su ./TouchGFX/generated/fonts/src/Font_verdana_16_4bpp_0.cyclo ./TouchGFX/generated/fonts/src/Font_verdana_16_4bpp_0.d ./TouchGFX/generated/fonts/src/Font_verdana_16_4bpp_0.o ./TouchGFX/generated/fonts/src/Font_verdana_16_4bpp_0.su ./TouchGFX/generated/fonts/src/Font_verdana_20_4bpp_0.cyclo ./TouchGFX/generated/fonts/src/Font_verdana_20_4bpp_0.d ./TouchGFX/generated/fonts/src/Font_verdana_20_4bpp_0.o ./TouchGFX/generated/fonts/src/Font_verdana_20_4bpp_0.su ./TouchGFX/generated/fonts/src/Font_verdana_40_4bpp_0.cyclo ./TouchGFX/generated/fonts/src/Font_verdana_40_4bpp_0.d ./TouchGFX/generated/fonts/src/Font_verdana_40_4bpp_0.o ./TouchGFX/generated/fonts/src/Font_verdana_40_4bpp_0.su ./TouchGFX/generated/fonts/src/GeneratedFont.cyclo ./TouchGFX/generated/fonts/src/GeneratedFont.d ./TouchGFX/generated/fonts/src/GeneratedFont.o ./TouchGFX/generated/fonts/src/GeneratedFont.su ./TouchGFX/generated/fonts/src/Kerning_verdana_10_4bpp.cyclo ./TouchGFX/generated/fonts/src/Kerning_verdana_10_4bpp.d ./TouchGFX/generated/fonts/src/Kerning_verdana_10_4bpp.o ./TouchGFX/generated/fonts/src/Kerning_verdana_10_4bpp.su ./TouchGFX/generated/fonts/src/Kerning_verdana_13_4bpp.cyclo ./TouchGFX/generated/fonts/src/Kerning_verdana_13_4bpp.d ./TouchGFX/generated/fonts/src/Kerning_verdana_13_4bpp.o ./TouchGFX/generated/fonts/src/Kerning_verdana_13_4bpp.su ./TouchGFX/generated/fonts/src/Kerning_verdana_16_4bpp.cyclo ./TouchGFX/generated/fonts/src/Kerning_verdana_16_4bpp.d ./TouchGFX/generated/fonts/src/Kerning_verdana_16_4bpp.o ./TouchGFX/generated/fonts/src/Kerning_verdana_16_4bpp.su ./TouchGFX/generated/fonts/src/Kerning_verdana_20_4bpp.cyclo ./TouchGFX/generated/fonts/src/Kerning_verdana_20_4bpp.d ./TouchGFX/generated/fonts/src/Kerning_verdana_20_4bpp.o ./TouchGFX/generated/fonts/src/Kerning_verdana_20_4bpp.su ./TouchGFX/generated/fonts/src/Kerning_verdana_40_4bpp.cyclo ./TouchGFX/generated/fonts/src/Kerning_verdana_40_4bpp.d ./TouchGFX/generated/fonts/src/Kerning_verdana_40_4bpp.o ./TouchGFX/generated/fonts/src/Kerning_verdana_40_4bpp.su ./TouchGFX/generated/fonts/src/Table_verdana_10_4bpp.cyclo ./TouchGFX/generated/fonts/src/Table_verdana_10_4bpp.d ./TouchGFX/generated/fonts/src/Table_verdana_10_4bpp.o ./TouchGFX/generated/fonts/src/Table_verdana_10_4bpp.su ./TouchGFX/generated/fonts/src/Table_verdana_13_4bpp.cyclo ./TouchGFX/generated/fonts/src/Table_verdana_13_4bpp.d ./TouchGFX/generated/fonts/src/Table_verdana_13_4bpp.o ./TouchGFX/generated/fonts/src/Table_verdana_13_4bpp.su ./TouchGFX/generated/fonts/src/Table_verdana_16_4bpp.cyclo ./TouchGFX/generated/fonts/src/Table_verdana_16_4bpp.d ./TouchGFX/generated/fonts/src/Table_verdana_16_4bpp.o ./TouchGFX/generated/fonts/src/Table_verdana_16_4bpp.su ./TouchGFX/generated/fonts/src/Table_verdana_20_4bpp.cyclo ./TouchGFX/generated/fonts/src/Table_verdana_20_4bpp.d ./TouchGFX/generated/fonts/src/Table_verdana_20_4bpp.o ./TouchGFX/generated/fonts/src/Table_verdana_20_4bpp.su ./TouchGFX/generated/fonts/src/Table_verdana_40_4bpp.cyclo ./TouchGFX/generated/fonts/src/Table_verdana_40_4bpp.d ./TouchGFX/generated/fonts/src/Table_verdana_40_4bpp.o ./TouchGFX/generated/fonts/src/Table_verdana_40_4bpp.su ./TouchGFX/generated/fonts/src/UnmappedDataFont.cyclo ./TouchGFX/generated/fonts/src/UnmappedDataFont.d ./TouchGFX/generated/fonts/src/UnmappedDataFont.o ./TouchGFX/generated/fonts/src/UnmappedDataFont.su ./TouchGFX/generated/fonts/src/VectorFontRendererBuffers.cyclo ./TouchGFX/generated/fonts/src/VectorFontRendererBuffers.d ./TouchGFX/generated/fonts/src/VectorFontRendererBuffers.o ./TouchGFX/generated/fonts/src/VectorFontRendererBuffers.su

.PHONY: clean-TouchGFX-2f-generated-2f-fonts-2f-src

