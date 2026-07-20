TARGET := STM32F407_FPGA_DDS
BUILD_DIR := build-gcc
FIRMWARE_DIR := Firmware
LINKER_SCRIPT := GCC/stm32f407vet6_flash.ld

CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE := $(CROSS_COMPILE)size

CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS := $(CPU_FLAGS) -std=c11 -Os -g3 -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -Wall -Wextra -Werror -pedantic \
	-MMD -MP -IInc
ASFLAGS := $(CPU_FLAGS) -x assembler-with-cpp
LDFLAGS := $(CPU_FLAGS) -nostartfiles -nostdlib -T$(LINKER_SCRIPT) \
	-Wl,--gc-sections,--build-id=none,-z,noexecstack \
	-Wl,-Map=$(BUILD_DIR)/$(TARGET).map

C_SOURCES := \
	Src/main.c \
	Src/fpga_dds.c \
	Src/model_learning.c \
	Src/calibration_store.c \
	Src/oled_ssd1306.c \
	Src/system_stm32f407_min.c
ASM_SOURCE := GCC/startup_stm32f407_gcc.s

OBJECTS := $(patsubst Src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) \
	$(BUILD_DIR)/startup_stm32f407_gcc.o
DEPENDENCIES := $(OBJECTS:.o=.d)
ELF := $(BUILD_DIR)/$(TARGET).elf
HEX := $(FIRMWARE_DIR)/$(TARGET).hex
BIN := $(FIRMWARE_DIR)/$(TARGET).bin

.PHONY: all clean size

all: $(HEX) $(BIN) size

$(BUILD_DIR) $(FIRMWARE_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: Src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/startup_stm32f407_gcc.o: $(ASM_SOURCE) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(ELF): $(OBJECTS) $(LINKER_SCRIPT)
	$(CC) $(LDFLAGS) $(OBJECTS) -lgcc -o $@

$(HEX): $(ELF) | $(FIRMWARE_DIR)
	$(OBJCOPY) -O ihex $< $@
	sed -i 's/\r$$//' $@

$(BIN): $(ELF) | $(FIRMWARE_DIR)
	$(OBJCOPY) -O binary $< $@

size: $(ELF)
	$(SIZE) $<

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(HEX) $(BIN)

-include $(DEPENDENCIES)
