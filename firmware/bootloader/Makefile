MCU := atmega32u4
ARCH := AVR8
LUFA_PATH := ../.deps/lufa/LUFA
VERSION_FILE := ../../VERSION
SOFTWARE_VERSION := $(strip $(shell sed -n '1p' $(VERSION_FILE)))
VERSION_PARTS := $(subst ., ,$(SOFTWARE_VERSION))
VERSION_VALID := $(shell printf '%s\n' '$(SOFTWARE_VERSION)' | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$$' && echo yes)
ifneq ($(VERSION_VALID),yes)
$(error $(VERSION_FILE) must contain three numeric components such as 0.12.0)
endif
VERSION_MAJOR := $(word 1,$(VERSION_PARTS))
VERSION_MINOR := $(word 2,$(VERSION_PARTS))
VERSION_PATCH := $(word 3,$(VERSION_PARTS))
.DEFAULT_GOAL := all
include $(LUFA_PATH)/Build/LUFA/lufa-sources.mk
SOURCES := BootloaderCDC.c BootloaderAPI.c Descriptors.c $(LUFA_SRC_USB_DEVICE)
OBJECTS := $(addprefix ../build/boot/,$(notdir $(SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(SOURCES)))
FLAGS := -mmcu=$(MCU) -Os -flto -std=gnu99 -ffunction-sections -fdata-sections -MMD -MP \
         -I. -IConfig -I../.deps/lufa -DF_CPU=16000000UL -DF_USB=16000000UL \
         -DARCH=ARCH_AVR8 -DBOARD=BOARD_NONE -DUSE_LUFA_CONFIG_HEADER -DBOOT_START_ADDR=0x7000 \
         -DP2000T_VERSION='"$(SOFTWARE_VERSION)"' -DP2000T_VERSION_MAJOR=$(VERSION_MAJOR) \
         -DP2000T_VERSION_MINOR=$(VERSION_MINOR) -DP2000T_VERSION_PATCH=$(VERSION_PATCH)
.PHONY: all
all: ../build/bootloader.hex
../build/boot/%.o: %.c
	@mkdir -p $(dir $@)
	avr-gcc $(FLAGS) -c $< -o $@
$(OBJECTS): $(VERSION_FILE)
../build/bootloader.elf: $(OBJECTS)
	avr-gcc -mmcu=$(MCU) -Os -flto -Wl,--gc-sections,--section-start=.text=0x7000,--defsym=__stack=0xafd,-Map=../build/bootloader.map $^ -o $@
	avr-size -C --mcu=$(MCU) $@
../build/bootloader.hex: ../build/bootloader.elf
	avr-objcopy -O ihex -R .eeprom $< $@
-include $(OBJECTS:.o=.d)
