# Fixture Engine::start Vita (crash run002952). Sin datos del juego.
TITLEID := HWAUDENG1
TARGET := audio-engine
BUILD := build
CXX := arm-vita-eabi-g++
CXXFLAGS := -std=gnu++17 -O2 -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -D__vita__ -I../../src
LIBS := -lstdc++ -lpthread -lSceAudio_stub -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceIofilemgr_stub -lc -lm
AUD := mixer wav music dsound_bus engine vita_sink
OBJECTS := $(BUILD)/main.o $(AUD:%=$(BUILD)/%.o)
all: $(BUILD)/$(TARGET).vpk
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/main.o: main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/%.o: ../../src/audio/%.cpp | $(BUILD)
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
