BUILD_TARGET = eeprom-programmer
BOARD_NAME = eeprom-programmer

CUBE_MAKEFILE_PATH = Cube-files

BUILD_DIR ?= Bin
EXTRA_LDFLAGS ?=

SRC_DIR := $(abspath Src)
SRC := $(wildcard $(SRC_DIR)/*.c)

INC_DIR := -I$(abspath Inc)

COLORS_ENABLED ?= 1
ifeq ($(COLORS_ENABLED), 1)
GREEN_COLOR = "\\033[92m"
BLUE_COLOR = "\\033[34m"
RED_COLOR = "\\033[91m"
NO_COLOR = "\\033[0m"
endif

CUSTOM_COMMANDS = all clean

.PHONY: $(CUSTOM_COMMANDS) $(BOARDS) 

DEBUG ?= 0
board ?=
BIN_DIR = Bin
BUILD_DIR = ../$(BIN_DIR)

BIN_FILE = $(BIN_DIR)/$(BOARD_NAME).elf

EXTRA_LDFLAGS = -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments -u _printf_float -u _scanf_float
EXTRA_CFLAGS ?= -Werror
all:
	@echo -e "$(BLUE_COLOR)Building: $(RED_COLOR)$(BUILD_TARGET)$(NO_COLOR)"
	@$(MAKE) --silent -C $(CUBE_MAKEFILE_PATH) BUILD_DIR="$(BUILD_DIR)" EXTRA_C_SRC="$(SRC)" EXTRA_C_INC="$(INC_DIR)" TARGET=$(BUILD_TARGET) EXTRA_LDFLAGS="$(EXTRA_LDFLAGS)" EXTRA_CFLAGS="$(EXTRA_CFLAGS)" DEBUG=$(DEBUG) --no-print-directory
	@echo -e "$(GREEN_COLOR)Completed$(NO_COLOR)"


flash:
	STM32_Programmer_CLI -c port=SWD -w $(BIN_FILE) 0x08000000 -v -hardRst

t6502:
	@echo -e "$(BLUE_COLOR)Building: $(RED_COLOR)65C02$(NO_COLOR)"
	vasm6502_oldstyle.exe .\6502\main.s -Fbin -wdc02 -dotdir -quiet -pad=0xea -wfail -o rom.bin
	@echo -e -n "$(GREEN_COLOR)65C02 ROM built: $(BLUE_COLOR)rom.bin$(NO_COLOR) "
	@stat -L -c %s rom.bin


flash_6502:
	@python .\send_bulk.py COM4 .\rom.bin

verify_6502:
	@python .\send_bulk.py COM4 .\rom.bin --verify-only

clean:
	rm -rf $(BIN_DIR)
	rm -f rom.bin

