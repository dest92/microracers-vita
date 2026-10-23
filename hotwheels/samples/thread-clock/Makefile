TITLEID := HWCLOCK01
TARGET := thread-clock
BUILD ?= build
CXX := arm-vita-eabi-g++
CXXFLAGS := -std=gnu++17 -O2 -Wall -Wextra -Werror -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -D__vita__ -I../../src
all: $(BUILD)/$(TARGET).vpk
$(BUILD):
	mkdir -p $@
$(BUILD)/main.o: main.cpp measurement.h ../../src/diag/thread_cpu.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/$(TARGET).elf: $(BUILD)/main.o
	$(CXX) -Wl,-q -o $@ $< -lScePower_stub -lSceIofilemgr_stub -lSceProcessmgr_stub -lSceLibKernel_stub -lSceKernelThreadMgr_stub
$(BUILD)/$(TARGET).velf: $(BUILD)/$(TARGET).elf
	vita-elf-create $< $@
$(BUILD)/eboot.bin: $(BUILD)/$(TARGET).velf
	vita-make-fself -s -c $< $@
$(BUILD)/param.sfo: | $(BUILD)
	vita-mksfoex -s TITLE_ID=$(TITLEID) "HW075 raw clock contrast" $@
$(BUILD)/$(TARGET).vpk: $(BUILD)/eboot.bin $(BUILD)/param.sfo
	vita-pack-vpk -s $(BUILD)/param.sfo -b $(BUILD)/eboot.bin $@
.PHONY: all
