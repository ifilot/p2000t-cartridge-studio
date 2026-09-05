MCU := atmega32u4
ARCH := AVR8
LUFA_PATH := .deps/lufa/LUFA
TARGET := build/cartridge
VERSION_FILE := ../VERSION
SOFTWARE_VERSION := $(strip $(shell sed -n '1p' $(VERSION_FILE)))
VERSION_PARTS := $(subst ., ,$(SOFTWARE_VERSION))
VERSION_VALID := $(shell printf '%s\n' '$(SOFTWARE_VERSION)' | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$$' && echo yes)
ifneq ($(VERSION_VALID),yes)
$(error $(VERSION_FILE) must contain three numeric components such as 0.12.0)
endif
VERSION_MAJOR := $(word 1,$(VERSION_PARTS))
VERSION_MINOR := $(word 2,$(VERSION_PARTS))
VERSION_PATCH := $(word 3,$(VERSION_PARTS))
VERSION_FLAGS := -DP2000T_VERSION='"$(SOFTWARE_VERSION)"' \
                 -DP2000T_VERSION_MAJOR=$(VERSION_MAJOR) \
                 -DP2000T_VERSION_MINOR=$(VERSION_MINOR) \
                 -DP2000T_VERSION_PATCH=$(VERSION_PATCH)
.DEFAULT_GOAL := all

ifeq ($(wildcard $(LUFA_PATH)/Build/LUFA/lufa-sources.mk),)
$(error LUFA missing: run sh fetch-lufa.sh first)
endif
include $(LUFA_PATH)/Build/LUFA/lufa-sources.mk

SOURCES := main.c protocol.c sst39sf020.c Descriptors.c $(LUFA_SRC_USB_DEVICE) \
           $(LUFA_PATH)/Drivers/USB/Class/Device/CDCClassDevice.c
OBJECTS := $(addprefix build/,$(SOURCES:.c=.o))
CPPFLAGS := -I. -I.deps/lufa -DF_CPU=16000000UL -DF_USB=16000000UL \
            -DARCH=ARCH_AVR8 -DBOARD=BOARD_NONE -DUSE_LUFA_CONFIG_HEADER \
            $(VERSION_FLAGS)
CFLAGS := -mmcu=$(MCU) -std=gnu99 -Os -g -Wall -Wextra \
          -ffunction-sections -fdata-sections -MMD -MP

.PHONY: all clean size
all: $(TARGET).hex size
$(TARGET).elf: $(OBJECTS)
	avr-gcc -mmcu=$(MCU) -Wl,--defsym=__stack=0xafd,--gc-sections,-Map=$(TARGET).map $^ -o $@
$(TARGET).raw.hex: $(TARGET).elf
	avr-objcopy -O ihex -R .eeprom $< $@
$(TARGET).hex: $(TARGET).raw.hex tools/application_image.py tools/hex_image.py
	python3 tools/application_image.py $< $@
build/%.o: %.c
	@mkdir -p $(dir $@)
	avr-gcc $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OBJECTS): $(VERSION_FILE)
size: $(TARGET).elf
	avr-size -C --mcu=$(MCU) $<
clean:
	rm -rf build
-include $(OBJECTS:.o=.d)

.PHONY: test
test: build/test-protocol
	./build/test-protocol
	python3 -m unittest tests/test_application_image.py
build/test-protocol: tests/test_protocol.c protocol.c protocol.h $(VERSION_FILE)
	@mkdir -p build
	gcc -std=c99 -Wall -Wextra -Werror -I. $(VERSION_FLAGS) tests/test_protocol.c protocol.c -o $@

.PHONY: bootloader combined
bootloader:
	$(MAKE) -C bootloader
combined: all bootloader
	python3 tools/hex_image.py build/cartridge.hex build/bootloader.hex build/combined.hex
