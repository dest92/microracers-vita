TITLEID := HWDRINGS
TARGET := d3d-ring
BUILD := build
VITASDK ?= ../../../shared/vitasdk
PREFIX := arm-vita-eabi
CXX := $(PREFIX)-g++
VGL_LIBS := -lvitaGL -lmathneon -lvitashark -lSceShaccCgExt -lSceShaccCg_stub \
  -lSceGxm_stub -lSceDisplay_stub -lSceCtrl_stub -lSceSysmodule_stub \
  -lSceCommonDialog_stub -lSceAppMgr_stub -lSceAppUtil_stub -lSceLibKernel_stub \
  -lSceKernelDmacMgr_stub -ltaihen_stub -lz -lc -lm
CXXFLAGS := -std=gnu++17 -O2 -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
  -D__vita__ -I../../src
SOURCES := main.cpp ../../src/d3d/core.cpp ../../src/d3d/vitagl_backend.cpp
OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(SOURCES)))

all: $(BUILD)/$(TARGET).vpk
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/main.o: main.cpp scene.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/core.o: ../../src/d3d/core.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/vitagl_backend.o: ../../src/d3d/vitagl_backend.cpp | $(BUILD)
	$(CXX) $(filter-out -O2,$(CXXFLAGS)) -Os -c $< -o $@
$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CXX) -Wl,-q -Wl,--defsym=__sce_headroom=0x1000 $^ $(VGL_LIBS) -lstdc++ -o $@
$(BUILD)/$(TARGET).velf: $(BUILD)/$(TARGET).elf
	vita-elf-create $< $@
$(BUILD)/eboot.bin: $(BUILD)/$(TARGET).velf
	vita-make-fself -s $< $@
$(BUILD)/param.sfo: | $(BUILD)
	vita-mksfoex -s TITLE_ID=$(TITLEID) "D3D ring smoke" $@
$(BUILD)/$(TARGET).vpk: $(BUILD)/eboot.bin $(BUILD)/param.sfo
	vita-pack-vpk -s $(BUILD)/param.sfo -b $(BUILD)/eboot.bin $@
clean:
	rm -rf $(BUILD)
.PHONY: all clean
