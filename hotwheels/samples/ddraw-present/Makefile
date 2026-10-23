# HW-021: VPK sintético P8, sin datos del juego.
TITLEID := HWPRSMPL1
TARGET := ddraw-present
BUILD := build
CXX := arm-vita-eabi-g++
CXXFLAGS := -std=gnu++17 -O2 -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -D__vita__ -I../../src
LIBS := -lvitaGL -lstdc++ -lmathneon -lvitashark -lSceShaccCgExt \
        -lSceShaccCg_stub -lSceGxm_stub -lSceDisplay_stub -lSceCtrl_stub \
        -lSceSysmodule_stub -lSceCommonDialog_stub -lSceAppMgr_stub \
        -lSceAppUtil_stub -lSceLibKernel_stub -lSceKernelDmacMgr_stub \
        -ltaihen_stub -lz -lc -lm
SOURCES := main.cpp ../../src/present/vita_present.cpp ../../src/present/present_math.cpp
OBJECTS := $(BUILD)/main.o $(BUILD)/vita_present.o $(BUILD)/present_math.o
all: $(BUILD)/$(TARGET).vpk
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/main.o: main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/vita_present.o: ../../src/present/vita_present.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/present_math.o: ../../src/present/present_math.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CXX) -Wl,-q -o $@ $^ $(LIBS)
$(BUILD)/$(TARGET).velf: $(BUILD)/$(TARGET).elf
	vita-elf-create $< $@
$(BUILD)/eboot.bin: $(BUILD)/$(TARGET).velf
	vita-make-fself -s $< $@
$(BUILD)/param.sfo: | $(BUILD)
	vita-mksfoex -s TITLE_ID=$(TITLEID) "$(TARGET)" $@
$(BUILD)/$(TARGET).vpk: $(BUILD)/eboot.bin $(BUILD)/param.sfo
	vita-pack-vpk -s $(BUILD)/param.sfo -b $(BUILD)/eboot.bin $@
clean:
	rm -rf $(BUILD)
.PHONY: all clean
