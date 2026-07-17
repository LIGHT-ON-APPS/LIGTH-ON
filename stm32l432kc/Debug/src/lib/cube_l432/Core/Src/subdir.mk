################################################################################
# Manually added for Cube-generated SPI handle source.
################################################################################

C_SRCS += \
../src/lib/cube_l432/Core/Src/spi.c

OBJS += \
./src/lib/cube_l432/Core/Src/spi.o

C_DEPS += \
./src/lib/cube_l432/Core/Src/spi.d


# Each subdirectory must supply rules for building sources it contributes
src/lib/cube_l432/Core/Src/%.o src/lib/cube_l432/Core/Src/%.su src/lib/cube_l432/Core/Src/%.cyclo: ../src/lib/cube_l432/Core/Src/%.c src/lib/cube_l432/Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DSTM32L432xx -c -I"C:/project/STM32L432KC/stm32l432kc/src" -I"C:/project/STM32L432KC/stm32l432kc/src/ap" -I"C:/project/STM32L432KC/stm32l432kc/src/bsp" -I"C:/project/STM32L432KC/stm32l432kc/src/common" -I"C:/project/STM32L432KC/stm32l432kc/src/common/core" -I"C:/project/STM32L432KC/stm32l432kc/src/common/hw/include" -I"C:/project/STM32L432KC/stm32l432kc/src/hw" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Core/Inc" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/STM32L4xx_HAL_Driver/Inc" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/cube_l432/Drivers/CMSIS/Device/ST/STM32L4xx/Include" -I"C:/project/STM32L432KC/stm32l432kc/src/lib/FatFs/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-src-2f-lib-2f-cube_l432-2f-Core-2f-Src

clean-src-2f-lib-2f-cube_l432-2f-Core-2f-Src:
	-$(RM) ./src/lib/cube_l432/Core/Src/spi.cyclo ./src/lib/cube_l432/Core/Src/spi.d ./src/lib/cube_l432/Core/Src/spi.o ./src/lib/cube_l432/Core/Src/spi.su

.PHONY: clean-src-2f-lib-2f-cube_l432-2f-Core-2f-Src

