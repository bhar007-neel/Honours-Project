# Builds the sensing-node firmware for the NUCLEO-H563ZI.
# Vendor trees (HAL, CMSIS, FreeRTOS) live in ../vendor and are not committed.

ROOT    := ../../..
CUBE    := ../vendor/STM32CubeH5
RTOS    := ../vendor/FreeRTOS-Kernel
GCC_DIR ?= C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin

CC      := $(GCC_DIR)/arm-none-eabi-gcc
OBJCOPY := $(GCC_DIR)/arm-none-eabi-objcopy

BUILD   := build
LDSCRIPT := $(CUBE)/Projects/NUCLEO-H563ZI/Templates/TrustZoneDisabled/STM32CubeIDE/STM32H563ZITX_FLASH.ld

CFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -std=c11 -Os -g \
          -ffunction-sections -fdata-sections -Wall -Wno-unused-parameter \
          -DSTM32H563xx -DUSE_HAL_DRIVER -DAPP_OUTPUT_TEXT=0
LDFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -specs=nano.specs -specs=nosys.specs \
           -Wl,--gc-sections -T$(LDSCRIPT) -Wl,--defsym=_sstack=0x2009F000

INC := -IInc -I../app -I$(ROOT)/shared/protocol -I$(ROOT)/shared/common \
       -I$(CUBE)/Drivers/STM32H5xx_HAL_Driver/Inc \
       -I$(CUBE)/Drivers/CMSIS/Device/ST/STM32H5xx/Include \
       -I$(CUBE)/Drivers/CMSIS/Include \
       -I$(RTOS)/include \
       -I$(RTOS)/portable/GCC/ARM_CM33_NTZ/non_secure

APP_SRC := \
  Src/main.c Src/stm32h5xx_it.c \
  ../app/app.c ../app/app_hooks.c ../app/board_nucleo_h563zi.c \
  $(ROOT)/shared/protocol/edge_protocol.c \
  $(ROOT)/shared/common/sensor_sim.c \
  $(ROOT)/shared/common/rt_stats.c

HAL_SRC := \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_cortex.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc_ex.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_gpio.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_uart.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_uart_ex.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr_ex.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash_ex.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_icache.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma.c \
  $(CUBE)/Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma_ex.c \
  $(CUBE)/Projects/NUCLEO-H563ZI/Templates/TrustZoneDisabled/Src/system_stm32h5xx.c

RTOS_SRC := \
  $(RTOS)/tasks.c $(RTOS)/queue.c $(RTOS)/list.c \
  $(RTOS)/portable/MemMang/heap_4.c \
  $(RTOS)/portable/GCC/ARM_CM33_NTZ/non_secure/port.c \
  $(RTOS)/portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c

STARTUP := $(CUBE)/Drivers/CMSIS/Device/ST/STM32H5xx/Source/Templates/gcc/startup_stm32h563xx.s

SRCS := $(APP_SRC) $(HAL_SRC) $(RTOS_SRC)
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o))) $(BUILD)/startup.o

.PHONY: all clean
all: $(BUILD)/edge-node.hex

$(BUILD):
	cmd /c "if not exist build mkdir build"

$(BUILD)/edge-node.hex: $(BUILD)/edge-node.elf
	$(OBJCOPY) -O ihex "$<" "$@"

$(BUILD)/edge-node.elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o "$@"

define compile
$(BUILD)/$(notdir $(1:.c=.o)): $(1) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c "$$<" -o "$$@"
endef
$(foreach src,$(SRCS),$(eval $(call compile,$(src))))

$(BUILD)/startup.o: $(STARTUP) | $(BUILD)
	$(CC) $(CFLAGS) -c "$<" -o "$@"

clean:
	rm -rf $(BUILD)
