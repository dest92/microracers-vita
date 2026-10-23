# vitagl-ffp — minimal FFP sample (HW-011)
# Requires VITASDK on PATH. Builds into ./build/ (gitignored).
TITLEID := HWFFPSMPL
TARGET  := vitagl-ffp
BUILD   := build

PREFIX := arm-vita-eabi
CC     := $(PREFIX)-gcc
CFLAGS := -g -Wl,-q -O2 -ftree-vectorize -std=gnu17

# Link order matters: vitaGL first, then its helpers, then the SCE stubs.
LIBS := -lvitaGL -lstdc++ -lmathneon -lvitashark -lSceShaccCgExt \
        -lSceShaccCg_stub -lSceGxm_stub -lSceDisplay_stub -lSceCtrl_stub \
        -lSceSysmodule_stub -lSceCommonDialog_stub -lSceAppMgr_stub \
        -lSceAppUtil_stub -lSceLibKernel_stub -lSceKernelDmacMgr_stub \
        -ltaihen_stub -lz -lc -lm

CFILES := $(wildcard *.c)
OBJS   := $(addprefix $(BUILD)/,$(CFILES:.c=.o))

all: $(BUILD)/$(TARGET).vpk

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

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
