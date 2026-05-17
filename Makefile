# PSP-only build for the Star Fox 64 port.
#
# Default target:
#   make
#   make psp
#
# Useful variants:
#   make PSP_FULL=0 psp    # debug/bootstrap EBOOT only
#   make bootstrap         # same, isolated under build/psp-bootstrap
#   make PROFILE_PSP=1 psp # profiling-friendly ELF packaging

-include .make_options

MAKEFLAGS += --no-builtin-rules --no-print-directory

TARGET := starfox64
VERSION ?= us
REV ?= rev1
PSP_FULL ?= 1
PROFILE_PSP ?= 0
COLOR ?= 1
VERBOSE ?= 0
N_THREADS ?= $(shell nproc 2>/dev/null || echo 1)
PYTHON ?= python3
TOOLS ?= tools

BASEROM := baserom.$(VERSION).$(REV).z64
BASEROM_UNCOMPRESSED := baserom.$(VERSION).$(REV).uncompressed.z64

COMPTOOL := $(TOOLS)/comptool.py
COMPTOOL_DIR := baserom
MIO0 := $(TOOLS)/mio0
SPLAT ?= $(PYTHON) $(TOOLS)/splat/split.py
SPLAT_YAML ?= $(TARGET).$(VERSION).$(REV).yaml
TORCH := $(TOOLS)/Torch/cmake-build-release/torch
CAT := cat

PSP_CONFIG ?= psp-config
PSP_FIXUP_IMPORTS ?= psp-fixup-imports
PSP_PRXGEN ?= psp-prxgen
MKSFOEX ?= mksfoex
PACK_PBP ?= pack-pbp
PSP_STRIP ?= psp-strip

PSPSDK ?= $(shell $(PSP_CONFIG) --pspsdk-path 2>/dev/null)
PSPDEV ?= $(shell $(PSP_CONFIG) --psp-prefix 2>/dev/null | sed 's,/psp$$,,')

CC := psp-gcc
OBJDUMP := psp-objdump
OBJCOPY := psp-objcopy

BUILD_DIR ?= build/psp
PSP_TITLE ?= Star Fox 64 PSP
PSP_EBOOT_DIR ?= src/psp/EBOOT
PSP_EBOOT_ICON ?= $(PSP_EBOOT_DIR)/ICON0.png
PSP_EBOOT_ICON1 ?= NULL
PSP_EBOOT_PIC0 ?= $(PSP_EBOOT_DIR)/PIC0.png
PSP_EBOOT_SND0 ?= $(PSP_EBOOT_DIR)/SND0.at3
PSP_EBOOT_PSAR ?= NULL

PSP_EBOOT := $(BUILD_DIR)/EBOOT.PBP
PSP_SFO := $(BUILD_DIR)/PARAM.SFO
PSP_ELF := $(BUILD_DIR)/$(TARGET).psp.elf
PSP_PRX := $(BUILD_DIR)/$(TARGET).psp.prx
PSP_MAP := $(BUILD_DIR)/$(TARGET).psp.map

PSP_LIBS ?= -lm -lpspdebug -lpspdisplay -lpspgu -lpspge -lpspctrl -lpspnet -lpspnet_apctl

ifeq ($(COLOR),1)
NO_COL := \033[0m
GREEN := \033[0;32m
BLUE := \033[0;34m
YELLOW := \033[0;33m
endif

ifeq ($(VERBOSE),0)
V := @
endif

define print
	@printf "$(GREEN)$(1) $(YELLOW)$(2)$(GREEN) -> $(BLUE)$(3)$(NO_COL)\n"
endef

COMMON_DEFINES := -D_MIPS_SZLONG=32
GBI_DEFINES := -DF3DEX_GBI
RELEASE_DEFINES := -DNDEBUG
VERSION_DEFINES := -DVERSION_US=1
PORT_DEFINES := -DLANGUAGE_C -D_LANGUAGE_C -DBUILD_VERSION=VERSION_H -DTARGET_PSP=1 -D_PSP=1
PORT_DEFINES += -DNON_MATCHING -DAVOID_UB -DCOMPILER_GCC

IINC := -Iinclude -Ibin/$(VERSION).$(REV) -I.
IINC += -Ilib/ultralib/include -Ilib/ultralib/include/PR -Ilib/ultralib/include/ido
IINC += -I$(PSPDEV)/psp/include -I$(PSPSDK)/include

CHECK_WARNINGS := -Wall -Wextra -Wimplicit-fallthrough -Wno-unknown-pragmas -Wno-missing-braces
CHECK_WARNINGS += -Wno-sign-compare -Wno-uninitialized

CFLAGS := -std=gnu89 -G0 -DPSP -D__PSP__ -D_PSP_FW_VERSION=500
CFLAGS += -O3 -Os -ffast-math -fno-unsafe-math-optimizations -fno-builtin -fno-common
CFLAGS += -fno-merge-constants -fno-strict-aliasing -falign-functions=64 -flimit-function-alignment
CFLAGS += -fno-rounding-math -ffp-contract=off -funsigned-char -MMD -MP
CFLAGS += $(CHECK_WARNINGS) $(VERSION_DEFINES) $(COMMON_DEFINES) $(RELEASE_DEFINES)
CFLAGS += $(GBI_DEFINES) $(PORT_DEFINES) $(IINC)
ifeq ($(PSP_FULL),1)
CFLAGS += -DPSP_FULL=1
endif

LDFLAGS := -L$(PSPDEV)/psp/lib -L$(PSPSDK)/lib
LDFLAGS += -Wl,-Map,$(PSP_MAP) -Wl,-zmax-page-size=128
ifeq ($(PROFILE_PSP),1)
CFLAGS += -pg
LDFLAGS += -pg
else
LDFLAGS += -specs=$(PSPSDK)/lib/prxspecs -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx $(PSPSDK)/lib/prxexports.o
endif

include src/psp/sources.mk

ifeq ($(PSP_FULL),1)
C_FILES := $(PSP_GAME_C_FILES)
else
C_FILES := $(PSP_BOOTSTRAP_C_FILES)
endif

O_FILES := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_FILES))
DEP_FILES := $(O_FILES:.o=.d)
ASSET_C_FILES := $(filter src/assets/%,$(PSP_GAME_C_FILES))
ASSET_PREFLIGHT_STAMP := $(BUILD_DIR)/asset-preflight.stamp

check-python-deps:
	@$(PYTHON) -c "import yaml" || (echo "Missing Python deps. Run: make python-deps  or  make venv" && false)

tools-init:
	git submodule update --init --recursive
	$(MAKE) -s -C $(TOOLS)
	$(RM) -f torch.hash.yml

init:
	$(MAKE) check-python-deps
	$(MAKE) tools-init
	$(MAKE) clean-generated
	$(MAKE) decompress
	$(MAKE) extract -j $(N_THREADS)
	$(MAKE) assets -j $(N_THREADS)
	$(MAKE) psp

decompress: $(BASEROM) $(MIO0)
	@echo "Decompressing ROM..."
	$(V)$(PYTHON) $(COMPTOOL) $(DECOMPRESS_OPT) -dse $(COMPTOOL_DIR) -m $(MIO0) $(BASEROM) $(BASEROM_UNCOMPRESSED)

extract: $(BASEROM_UNCOMPRESSED)
	$(RM) -r asm/$(VERSION)/$(REV) bin/$(VERSION)/$(REV)
	@mkdir -p asm bin
	@echo "Unifying yamls..."
	$(V)$(CAT) yamls/$(VERSION)/$(REV)/header.yaml yamls/$(VERSION)/$(REV)/main.yaml yamls/$(VERSION)/$(REV)/assets.yaml yamls/$(VERSION)/$(REV)/overlays.yaml > $(SPLAT_YAML)
	@echo "Extracting..."
	$(V)$(SPLAT) $(SPLAT_YAML)

assets: $(BASEROM_UNCOMPRESSED)
	@echo "Extracting assets from ROM..."
	$(V)$(TORCH) code $(BASEROM_UNCOMPRESSED)
	$(V)$(TORCH) header $(BASEROM_UNCOMPRESSED)
	$(V)$(TORCH) modding export $(BASEROM_UNCOMPRESSED)

clean-generated:
	$(RM) -f torch.hash.yml $(SPLAT_YAML)
	$(RM) -r asm/$(VERSION)/$(REV)
	$(RM) -r bin/$(VERSION)/$(REV)
	$(RM) -r src/assets
	$(RM) -r include/assets

all: psp

psp: $(PSP_EBOOT)
	@printf "$(BLUE)$(PSP_EBOOT)$(NO_COL): $(GREEN)OK$(NO_COL)\n"

bootstrap:
	$(MAKE) PSP_FULL=0 BUILD_DIR=build/psp-bootstrap psp

preflight:
ifeq ($(PSP_FULL),1)
	$(V)$(PYTHON) tools/psp_validate_assets.py
endif

ifeq ($(PSP_FULL),1)
$(PSP_ELF): $(ASSET_PREFLIGHT_STAMP)

$(ASSET_PREFLIGHT_STAMP): tools/psp_validate_assets.py $(ASSET_C_FILES) include/sf64object.h include/sf64event.h
	@mkdir -p $(dir $@)
	$(V)$(PYTHON) tools/psp_validate_assets.py
	$(V)touch $@
endif

$(MIO0):
	$(MAKE) -s -C $(TOOLS)

$(PSP_EBOOT): $(PSP_ELF) $(PSP_SFO)
	$(call print,Packaging PSP EBOOT:,$<,$@)
ifeq ($(PROFILE_PSP),1)
	$(V)$(PSP_STRIP) $(PSP_ELF) -o $(BUILD_DIR)/$(TARGET).psp.strip.elf
	$(V)$(PACK_PBP) $@ $(PSP_SFO) $(PSP_EBOOT_ICON) $(PSP_EBOOT_ICON1) NULL $(PSP_EBOOT_PIC0) $(PSP_EBOOT_SND0) $(BUILD_DIR)/$(TARGET).psp.strip.elf $(PSP_EBOOT_PSAR)
	$(V)$(RM) -f $(BUILD_DIR)/$(TARGET).psp.strip.elf
else
	$(V)$(PSP_PRXGEN) $(PSP_ELF) $(PSP_PRX)
	$(V)$(PACK_PBP) $@ $(PSP_SFO) $(PSP_EBOOT_ICON) $(PSP_EBOOT_ICON1) NULL $(PSP_EBOOT_PIC0) $(PSP_EBOOT_SND0) $(PSP_PRX) $(PSP_EBOOT_PSAR)
endif

$(PSP_SFO):
	@mkdir -p $(dir $@)
	$(call print,Generating PSP SFO:,$(PSP_TITLE),$@)
	$(V)$(MKSFOEX) -d MEMSIZE=1 '$(PSP_TITLE)' $@

$(PSP_ELF): $(O_FILES)
	@mkdir -p $(dir $@)
	$(call print,Linking PSP ELF:,$<,$@)
	$(V)$(CC) $(O_FILES) $(LDFLAGS) $(PSP_LIBS) -o $@
ifneq ($(PROFILE_PSP),1)
	$(V)$(PSP_FIXUP_IMPORTS) $@
endif

$(BUILD_DIR)/%.o: %.c Makefile src/psp/sources.mk
	@mkdir -p $(dir $@)
	$(call print,Compiling:,$<,$@)
	$(V)$(CC) -c $(CFLAGS) -I$(dir $*) -o $@ $<

clean:
	$(RM) -r $(BUILD_DIR) build/psp-bootstrap

print-%:
	$(info $* is a $(flavor $*) variable set to [$($*)])
	@true

-include $(DEP_FILES)

.PHONY: tools-init toolchain torch init decompress extract assets clean-generated
