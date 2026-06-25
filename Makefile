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
.DEFAULT_GOAL := psp

TARGET := starfox64
VERSION ?= us
REV ?= rev1
PSP_FULL ?= 1
PROFILE_PSP ?= 0
SF64_PSP_PROFILE_PHASES ?= 0
SF64_PSP_PROFILE_CAPTURE_FRAMES ?= 300
SF64_PSP_PSPGL_VBO_STREAM ?= 1
SF64_PSP_DIRECT_TRI_FASTPATH ?= 1
SF64_PSP_BATCH_STATE_CACHE ?= 1
PSP_LOG ?= 0
PSP_TRACE ?= 0
PSP_RENDERER_DIAGNOSTICS ?= 0
PSP_AUDIO_SYNTH ?= 0
PSP_AUDIO_OUTPUT ?= 0
PSP_FPS_OVERLAY ?= 1
N64PSP_USE_VFPU ?= 1
USE_N64PSP_MATH ?= 1
PSP_VALIDATE_N64PSP_MATH ?= 0
USE_N64PSP_BATCH_TRANSFORM ?= 1
PSP_VALIDATE_N64PSP_BATCH_TRANSFORM ?= 0
USE_N64PSP_BATCH_LIGHTING ?= 1
PSP_VALIDATE_N64PSP_BATCH_LIGHTING ?= 0
USE_N64PSP_QUEUES ?= 1
N64PSP_QUEUE_SELFTEST ?= 0
N64PSP_QUEUE_TRACE ?= 0
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
PSPGL_CONFIG ?= pspgl-config
PSP_NM ?= psp-nm
SHA256SUM ?= sha256sum
DATE ?= date

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
PROFILE_METADATA := $(BUILD_DIR)/profile_build_metadata.txt
PROFILE_BUILD_COMMANDS := $(BUILD_DIR)/PROFILE_BUILD_COMMANDS.txt
PROFILE_SHA256SUMS := $(BUILD_DIR)/SHA256SUMS
PROFILE_ARTIFACT_ROOT ?= artifacts
QUEUE_TRACE_LOG ?= sf64_psp.log
QUEUE_TRACE_QUEUES ?= 0x9917470 0x9974c14 0x9974ca4 0x9974c84
PSP_LOAD_BASE ?= 0x08804000

PSP_LIBS ?= -lm -lpspdebug -lpspdisplay -lpspgu -lpspge -lpspctrl -lpspaudio -lpsppower

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
ifneq ($(filter 1,$(USE_N64PSP_QUEUES) $(USE_N64PSP_MATH)),)
IINC += -Ilib/n64psp/include
endif

PSP_WARNINGS := -Wall -Wextra -Wimplicit-fallthrough -Wno-unknown-pragmas -Wno-missing-braces
PSP_WARNINGS += -Wno-sign-compare -Wno-uninitialized

QUIET_DECOMP ?= 1

define source_warning_flags
$(if $(filter src/psp/%,$<),$(PSP_WARNINGS),$(if $(filter 1,$(QUIET_DECOMP)),-w,$(PSP_WARNINGS)))
endef

PSP_OPTFLAGS ?= -O2 # -ffast-math
SF64_GIT_SHA := $(shell git rev-parse HEAD 2>/dev/null || echo unknown)
N64PSP_GIT_SHA := $(shell git -C lib/n64psp rev-parse HEAD 2>/dev/null || echo unknown)
SF64_GIT_DIRTY := $(shell test -z "$$(git status --porcelain 2>/dev/null)" && echo clean || echo dirty)
N64PSP_GIT_DIRTY := $(shell test -z "$$(git -C lib/n64psp status --porcelain 2>/dev/null)" && echo clean || echo dirty)
PERFECT_DARK_PSP_SHA := 0871c907aea105cd2e7002219d047c733011f668
PSP_COMPILER_VERSION := $(shell $(CC) --version 2>/dev/null | sed -n '1p' | sed 's/"/\\"/g; s/[[:space:]]\+/_/g')

ifeq ($(PROFILE_PSP),1)
ifeq ($(SF64_PSP_PROFILE_PHASES),1)
PSP_PROFILE_MODE := combined
else
PSP_PROFILE_MODE := gprof
endif
else ifeq ($(SF64_PSP_PROFILE_PHASES),1)
PSP_PROFILE_MODE := phases
else
PSP_PROFILE_MODE := release
endif

ifeq ($(filter 1,$(PROFILE_PSP) $(SF64_PSP_PROFILE_PHASES)),)
PROFILE_OUTPUTS :=
else
PROFILE_OUTPUTS := $(PROFILE_METADATA) $(PROFILE_BUILD_COMMANDS) $(PROFILE_SHA256SUMS)
endif

CFLAGS := -std=gnu89 -G0 -DPSP -D__PSP__ -D_PSP_FW_VERSION=600 
CFLAGS += $(PSP_OPTFLAGS) -g3
CFLAGS += -fno-builtin -fno-common -fno-strict-aliasing
CFLAGS += -fwrapv -funsigned-char
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fno-exceptions -fno-unwind-tables
CFLAGS += -fno-asynchronous-unwind-tables -fno-ident
CFLAGS += -DUSE_N64PSP_MATH=$(if $(filter 1,$(USE_N64PSP_MATH)),1,0)
CFLAGS += -DN64PSP_USE_VFPU=$(if $(filter 1,$(N64PSP_USE_VFPU)),1,0)
CFLAGS += -DPSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY)
CFLAGS += -DPSP_AUDIO_SYNTH=$(PSP_AUDIO_SYNTH)
CFLAGS += -DPSP_AUDIO_OUTPUT=$(PSP_AUDIO_OUTPUT)
CFLAGS += -DSF64_PSP_GPROF=$(if $(filter 1,$(PROFILE_PSP)),1,0)
CFLAGS += -DSF64_PSP_PROFILE_PHASES=$(if $(filter 1,$(SF64_PSP_PROFILE_PHASES)),1,0)
CFLAGS += -DSF64_PSP_PROFILE_CAPTURE_FRAMES=$(SF64_PSP_PROFILE_CAPTURE_FRAMES)
CFLAGS += -DSF64_PSP_PSPGL_VBO_STREAM=$(if $(filter 1,$(SF64_PSP_PSPGL_VBO_STREAM)),1,0)
CFLAGS += -DSF64_PSP_DIRECT_TRI_FASTPATH=$(if $(filter 1,$(SF64_PSP_DIRECT_TRI_FASTPATH)),1,0)
CFLAGS += -DSF64_PSP_BATCH_STATE_CACHE=$(if $(filter 1,$(SF64_PSP_BATCH_STATE_CACHE)),1,0)
CFLAGS += '-DSF64_GIT_SHA="$(SF64_GIT_SHA)"'
CFLAGS += '-DN64PSP_GIT_SHA="$(N64PSP_GIT_SHA)"'
CFLAGS += '-DPERFECT_DARK_PSP_SHA="$(PERFECT_DARK_PSP_SHA)"'
CFLAGS += '-DSF64_PSP_COMPILER="$(PSP_COMPILER_VERSION)"'
CFLAGS += '-DSF64_PSP_OPT_FLAGS="$(PSP_OPTFLAGS)"'
CFLAGS += -DPSP_VALIDATE_N64PSP_MATH=$(PSP_VALIDATE_N64PSP_MATH)
CFLAGS += -DUSE_N64PSP_BATCH_TRANSFORM=$(if $(filter 1,$(USE_N64PSP_BATCH_TRANSFORM)),1,0)
CFLAGS += -DPSP_VALIDATE_N64PSP_BATCH_TRANSFORM=$(if $(filter 1,$(PSP_VALIDATE_N64PSP_BATCH_TRANSFORM)),1,0)
CFLAGS += -DUSE_N64PSP_BATCH_LIGHTING=$(if $(filter 1,$(USE_N64PSP_BATCH_LIGHTING)),1,0)
CFLAGS += -DPSP_VALIDATE_N64PSP_BATCH_LIGHTING=$(if $(filter 1,$(PSP_VALIDATE_N64PSP_BATCH_LIGHTING)),1,0)
CFLAGS += $(VERSION_DEFINES) $(COMMON_DEFINES) $(RELEASE_DEFINES)
CFLAGS += $(GBI_DEFINES) $(PORT_DEFINES) $(IINC)

ifeq ($(PSP_FULL),1)
CFLAGS += -DPSP_FULL=1
endif
ifeq ($(PSP_LOG),1)
CFLAGS += -DPSP_LOG_ENABLED=1
endif
ifeq ($(PSP_TRACE),1)
CFLAGS += -DPSP_TRACE_ENABLED=1
endif
ifeq ($(PSP_RENDERER_DIAGNOSTICS),1)
CFLAGS += -DPSP_RENDERER_DIAGNOSTICS=1
endif
ifeq ($(USE_N64PSP_QUEUES),1)
CFLAGS += -DUSE_N64PSP_QUEUES=1
endif
ifeq ($(N64PSP_QUEUE_SELFTEST),1)
CFLAGS += -DN64PSP_QUEUE_SELFTEST=1
endif
ifeq ($(N64PSP_QUEUE_TRACE),1)
CFLAGS += -DN64PSP_QUEUE_TRACE=1
endif
ifeq ($(USE_N64PSP_BATCH_TRANSFORM),1)
ifneq ($(USE_N64PSP_MATH),1)
$(error USE_N64PSP_BATCH_TRANSFORM=1 requires USE_N64PSP_MATH=1)
endif
endif
ifeq ($(USE_N64PSP_BATCH_LIGHTING),1)
ifneq ($(USE_N64PSP_MATH),1)
$(error USE_N64PSP_BATCH_LIGHTING=1 requires USE_N64PSP_MATH=1)
endif
endif
ifeq ($(PSP_VALIDATE_N64PSP_BATCH_LIGHTING),1)
ifneq ($(USE_N64PSP_BATCH_LIGHTING),1)
$(error PSP_VALIDATE_N64PSP_BATCH_LIGHTING=1 requires USE_N64PSP_BATCH_LIGHTING=1)
endif
endif
PSPGL_CONFIG_PATH := $(shell command -v $(PSPGL_CONFIG) 2>/dev/null)
ifneq ($(PSPGL_CONFIG_PATH),)
PSPGL_CFLAGS := $(shell $(PSPGL_CONFIG) --cflags)
PSPGL_LIBS := $(shell $(PSPGL_CONFIG) --libs)
else
PSPGL_HEADER := $(firstword $(wildcard $(PSPDEV)/psp/include/GLES/egl.h) $(wildcard $(PSPDEV)/psp/include/GL/gl.h))
PSPGL_LIBRARY := $(firstword $(wildcard $(PSPDEV)/psp/lib/libGL.a))
ifeq ($(PSPGL_HEADER),)
$(error PSPGL not found. Install PSPGL before building the PSP port.)
endif
ifeq ($(PSPGL_LIBRARY),)
$(error PSPGL not found. Install PSPGL before building the PSP port.)
endif
ifeq ($(USE_N64PSP_BATCH_TRANSFORM),1)
ifneq ($(USE_N64PSP_MATH),1)
$(error USE_N64PSP_BATCH_TRANSFORM=1 requires USE_N64PSP_MATH=1)
endif
endif
ifeq ($(USE_N64PSP_BATCH_LIGHTING),1)
ifneq ($(USE_N64PSP_MATH),1)
$(error USE_N64PSP_BATCH_LIGHTING=1 requires USE_N64PSP_MATH=1)
endif
endif
ifeq ($(PSP_VALIDATE_N64PSP_BATCH_LIGHTING),1)
ifneq ($(USE_N64PSP_BATCH_LIGHTING),1)
$(error PSP_VALIDATE_N64PSP_BATCH_LIGHTING=1 requires USE_N64PSP_BATCH_LIGHTING=1)
endif
endif
PSPGL_CFLAGS :=
PSPGL_LIBS := -lGL -lpspvfpu
endif
CFLAGS += $(PSPGL_CFLAGS)
PSP_LIBS := $(PSPGL_LIBS) $(PSP_LIBS)

N64PSP_DIR := lib/n64psp
N64PSP_BUILD_PSP := build-psp-$(PSP_PROFILE_MODE)

N64PSP_PSP_ARCHIVES :=
N64PSP_BUILD_TARGETS :=
N64PSP_BUILD_STAMP := $(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/.sf64-n64psp-build.stamp

ifeq ($(USE_N64PSP_QUEUES),1)
N64PSP_PSP_ARCHIVES += \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_runtime.a \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_trace_backend.a \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_platform_psp.a

N64PSP_BUILD_TARGETS += \
	$(N64PSP_BUILD_PSP)/libn64psp_runtime.a \
	$(N64PSP_BUILD_PSP)/libn64psp_trace_backend.a \
	$(N64PSP_BUILD_PSP)/libn64psp_platform_psp.a
endif

ifeq ($(USE_N64PSP_MATH),1)
N64PSP_PSP_ARCHIVES += \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_math.a

N64PSP_BUILD_TARGETS += \
	$(N64PSP_BUILD_PSP)/libn64psp_math.a
endif

LDFLAGS := -L$(PSPDEV)/psp/lib -L$(PSPSDK)/lib
LDFLAGS += -Wl,-Map,$(PSP_MAP) -Wl,-zmax-page-size=128

ifeq ($(PROFILE_PSP),1)
CFLAGS += -pg -g -fno-omit-frame-pointer -fno-optimize-sibling-calls
LDFLAGS += -pg -g
endif

LDFLAGS += -specs=$(PSPSDK)/lib/prxspecs \
           -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx \
           $(PSPSDK)/lib/prxexports.o

include src/psp/sources.mk

ifeq ($(PSP_FULL),1)
C_FILES := $(PSP_GAME_C_FILES)
else
C_FILES := $(PSP_BOOTSTRAP_C_FILES)
endif

O_FILES := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_FILES))
ifeq ($(PSP_FULL),1)
O_FILES += $(patsubst %.S,$(BUILD_DIR)/%.o,$(PSP_GAME_S_FILES))
endif
ifneq ($(strip $(N64PSP_PSP_ARCHIVES)),)
O_FILES += $(N64PSP_PSP_ARCHIVES)
endif
DEP_FILES := $(patsubst %.o,%.d,$(filter %.o,$(O_FILES)))
ASSET_C_FILES := $(filter src/assets/%,$(PSP_GAME_C_FILES))
ASSET_PREFLIGHT_STAMP := $(BUILD_DIR)/asset-preflight.stamp
COMPILE_FLAGS_STAMP := $(BUILD_DIR)/compile-flags.stamp

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

psp: $(PSP_EBOOT) $(PROFILE_OUTPUTS)
	@printf "$(BLUE)$(PSP_EBOOT)$(NO_COL): $(GREEN)OK$(NO_COL)\n"

bootstrap:
	$(MAKE) PSP_FULL=0 BUILD_DIR=build/psp-bootstrap psp

psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts: PSP_FPS_OVERLAY=0
psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts: PSP_LOG=0
psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts: PSP_TRACE=0
psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts: PSP_RENDERER_DIAGNOSTICS=0
psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts: PSP_VALIDATE_N64PSP_MATH=0
psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts: PSP_VALIDATE_N64PSP_BATCH_TRANSFORM=0
psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts: N64PSP_QUEUE_TRACE=0

psp-profile-gprof:
	$(MAKE) PROFILE_PSP=1 SF64_PSP_PROFILE_PHASES=0 BUILD_DIR=build/psp-profile-gprof PSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY) PSP_LOG=$(PSP_LOG) PSP_TRACE=$(PSP_TRACE) PSP_RENDERER_DIAGNOSTICS=$(PSP_RENDERER_DIAGNOSTICS) PSP_VALIDATE_N64PSP_MATH=$(PSP_VALIDATE_N64PSP_MATH) PSP_VALIDATE_N64PSP_BATCH_TRANSFORM=$(PSP_VALIDATE_N64PSP_BATCH_TRANSFORM) N64PSP_QUEUE_TRACE=$(N64PSP_QUEUE_TRACE) psp

psp-profile-phases:
	$(MAKE) PROFILE_PSP=0 SF64_PSP_PROFILE_PHASES=1 BUILD_DIR=build/psp-profile-phases PSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY) PSP_LOG=$(PSP_LOG) PSP_TRACE=$(PSP_TRACE) PSP_RENDERER_DIAGNOSTICS=$(PSP_RENDERER_DIAGNOSTICS) PSP_VALIDATE_N64PSP_MATH=$(PSP_VALIDATE_N64PSP_MATH) PSP_VALIDATE_N64PSP_BATCH_TRANSFORM=$(PSP_VALIDATE_N64PSP_BATCH_TRANSFORM) N64PSP_QUEUE_TRACE=$(N64PSP_QUEUE_TRACE) psp

psp-profile-combined:
	$(MAKE) PROFILE_PSP=1 SF64_PSP_PROFILE_PHASES=1 BUILD_DIR=build/psp-profile-combined PSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY) PSP_LOG=$(PSP_LOG) PSP_TRACE=$(PSP_TRACE) PSP_RENDERER_DIAGNOSTICS=$(PSP_RENDERER_DIAGNOSTICS) PSP_VALIDATE_N64PSP_MATH=$(PSP_VALIDATE_N64PSP_MATH) PSP_VALIDATE_N64PSP_BATCH_TRANSFORM=$(PSP_VALIDATE_N64PSP_BATCH_TRANSFORM) N64PSP_QUEUE_TRACE=$(N64PSP_QUEUE_TRACE) psp

psp-profile-builds: psp-profile-gprof psp-profile-phases

psp-profile-artifacts: psp-profile-builds
	@set -eu; \
	stamp="$$( $(DATE) -u +%Y%m%dT%H%M%SZ )"; \
	dest="$(PROFILE_ARTIFACT_ROOT)/psp-profile-$$stamp"; \
	mkdir -p "$$dest/builds/gprof" "$$dest/builds/phases" "$$dest/raw" "$$dest/reports"; \
	cp -f build/psp-profile-gprof/EBOOT.PBP build/psp-profile-gprof/$(TARGET).psp.elf build/psp-profile-gprof/$(TARGET).psp.map build/psp-profile-gprof/profile_build_metadata.txt build/psp-profile-gprof/PROFILE_BUILD_COMMANDS.txt build/psp-profile-gprof/SHA256SUMS "$$dest/builds/gprof/"; \
	cp -f build/psp-profile-phases/EBOOT.PBP build/psp-profile-phases/$(TARGET).psp.elf build/psp-profile-phases/$(TARGET).psp.map build/psp-profile-phases/profile_build_metadata.txt build/psp-profile-phases/PROFILE_BUILD_COMMANDS.txt build/psp-profile-phases/SHA256SUMS "$$dest/builds/phases/"; \
	( cd "$$dest" && $(SHA256SUM) builds/gprof/* builds/phases/* > SHA256SUMS ); \
	printf '%s\n' "$$dest"

psp-profile-report:
	$(PYTHON) tools/psp_profile_report.py "$(ELF)" "$(GMON)" "$(OUT)"

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
	$(V)$(PSP_PRXGEN) $(PSP_ELF) $(PSP_PRX)
	$(V)$(PACK_PBP) $@ $(PSP_SFO) $(PSP_EBOOT_ICON) $(PSP_EBOOT_ICON1) NULL $(PSP_EBOOT_PIC0) $(PSP_EBOOT_SND0) $(PSP_PRX) $(PSP_EBOOT_PSAR)

$(PROFILE_METADATA): $(PSP_EBOOT) $(PSP_ELF) $(PSP_MAP) Makefile
	@mkdir -p $(dir $@)
	$(call print,Writing profile metadata:,$(PSP_PROFILE_MODE),$@)
	$(V){ \
		printf 'profile_mode=%s\n' '$(PSP_PROFILE_MODE)'; \
		printf 'build_id=%s-%s-%s-sf64_%s-n64psp_%s\n' '$(PSP_PROFILE_MODE)' '$(SF64_GIT_SHA)' '$(N64PSP_GIT_SHA)' '$(SF64_GIT_DIRTY)' '$(N64PSP_GIT_DIRTY)'; \
		printf 'sf64_commit=%s\n' '$(SF64_GIT_SHA)'; \
		printf 'sf64_worktree=%s\n' '$(SF64_GIT_DIRTY)'; \
		printf 'n64psp_commit=%s\n' '$(N64PSP_GIT_SHA)'; \
		printf 'n64psp_worktree=%s\n' '$(N64PSP_GIT_DIRTY)'; \
		printf 'compiler=%s\n' '$(PSP_COMPILER_VERSION)'; \
		printf 'cflags=%s\n' '$(CFLAGS)'; \
		printf 'ldflags=%s\n' '$(LDFLAGS)'; \
		printf 'psp_libs=%s\n' '$(PSP_LIBS)'; \
		printf 'n64psp_build_dir=%s\n' '$(N64PSP_BUILD_PSP)'; \
		printf 'PROFILE_PSP=%s\n' '$(PROFILE_PSP)'; \
		printf 'SF64_PSP_PROFILE_PHASES=%s\n' '$(SF64_PSP_PROFILE_PHASES)'; \
		printf 'SF64_PSP_PROFILE_CAPTURE_FRAMES=%s\n' '$(SF64_PSP_PROFILE_CAPTURE_FRAMES)'; \
		printf 'SF64_PSP_PSPGL_VBO_STREAM=%s\n' '$(SF64_PSP_PSPGL_VBO_STREAM)'; \
		printf 'SF64_PSP_DIRECT_TRI_FASTPATH=%s\n' '$(SF64_PSP_DIRECT_TRI_FASTPATH)'; \
		printf 'SF64_PSP_BATCH_STATE_CACHE=%s\n' '$(SF64_PSP_BATCH_STATE_CACHE)'; \
		printf 'PSP_FPS_OVERLAY=%s\n' '$(PSP_FPS_OVERLAY)'; \
		printf 'PSP_LOG=%s\n' '$(PSP_LOG)'; \
		printf 'PSP_TRACE=%s\n' '$(PSP_TRACE)'; \
		printf 'PSP_RENDERER_DIAGNOSTICS=%s\n' '$(PSP_RENDERER_DIAGNOSTICS)'; \
		printf 'PSP_VALIDATE_N64PSP_MATH=%s\n' '$(PSP_VALIDATE_N64PSP_MATH)'; \
		printf 'PSP_VALIDATE_N64PSP_BATCH_TRANSFORM=%s\n' '$(PSP_VALIDATE_N64PSP_BATCH_TRANSFORM)'; \
		printf 'N64PSP_QUEUE_TRACE=%s\n' '$(N64PSP_QUEUE_TRACE)'; \
		printf 'cpu_clock_runtime=recorded in profile-NNN.txt on PSP\n'; \
		printf 'bus_clock_runtime=recorded in profile-NNN.txt on PSP\n'; \
		printf 'build_command=make %s BUILD_DIR=%s PROFILE_PSP=%s SF64_PSP_PROFILE_PHASES=%s SF64_PSP_PSPGL_VBO_STREAM=%s SF64_PSP_DIRECT_TRI_FASTPATH=%s SF64_PSP_BATCH_STATE_CACHE=%s PSP_FPS_OVERLAY=%s PSP_LOG=%s PSP_TRACE=%s PSP_RENDERER_DIAGNOSTICS=%s PSP_VALIDATE_N64PSP_MATH=%s PSP_VALIDATE_N64PSP_BATCH_TRANSFORM=%s N64PSP_QUEUE_TRACE=%s psp\n' '$(MAKECMDGOALS)' '$(BUILD_DIR)' '$(PROFILE_PSP)' '$(SF64_PSP_PROFILE_PHASES)' '$(SF64_PSP_PSPGL_VBO_STREAM)' '$(SF64_PSP_DIRECT_TRI_FASTPATH)' '$(SF64_PSP_BATCH_STATE_CACHE)' '$(PSP_FPS_OVERLAY)' '$(PSP_LOG)' '$(PSP_TRACE)' '$(PSP_RENDERER_DIAGNOSTICS)' '$(PSP_VALIDATE_N64PSP_MATH)' '$(PSP_VALIDATE_N64PSP_BATCH_TRANSFORM)' '$(N64PSP_QUEUE_TRACE)'; \
	} > $@

$(PROFILE_BUILD_COMMANDS): $(PROFILE_METADATA)
	@mkdir -p $(dir $@)
	$(call print,Writing profile commands:,$(PSP_PROFILE_MODE),$@)
	$(V){ \
		printf 'make psp-profile-gprof\n'; \
		printf 'make psp-profile-phases\n'; \
		printf 'make psp-profile-builds\n'; \
		printf 'make psp-profile-artifacts\n'; \
		printf '\nActual build command for this output:\n'; \
		grep '^build_command=' $(PROFILE_METADATA) | sed 's/^build_command=//'; \
	} > $@

$(PROFILE_SHA256SUMS): $(PSP_EBOOT) $(PSP_ELF) $(PSP_MAP) $(PROFILE_METADATA) $(PROFILE_BUILD_COMMANDS)
	@mkdir -p $(dir $@)
	$(call print,Writing profile checksums:,$(PSP_PROFILE_MODE),$@)
	$(V)( cd $(BUILD_DIR) && $(SHA256SUM) EBOOT.PBP $(TARGET).psp.elf $(TARGET).psp.map profile_build_metadata.txt PROFILE_BUILD_COMMANDS.txt > SHA256SUMS )

$(PSP_SFO):
	@mkdir -p $(dir $@)
	$(call print,Generating PSP SFO:,$(PSP_TITLE),$@)
	$(V)$(MKSFOEX) -d MEMSIZE=1 '$(PSP_TITLE)' $@

$(PSP_ELF): $(O_FILES)
	@mkdir -p $(dir $@)
	$(call print,Linking PSP ELF:,$<,$@)
	$(V)$(CC) $(O_FILES) $(LDFLAGS) $(PSP_LIBS) -o $@
	$(V)$(PSP_FIXUP_IMPORTS) $@

$(PSP_MAP): $(PSP_ELF)
	@test -f $@

$(N64PSP_BUILD_STAMP): FORCE
	$(MAKE) -C $(N64PSP_DIR) \
		BUILD_PSP=$(N64PSP_BUILD_PSP) \
		PSP_CONFIG=$(PSP_CONFIG) \
		PSP_CC=$(CC) \
		PSP_AR=psp-ar \
		N64PSP_PROFILE_PSP=$(if $(filter 1,$(PROFILE_PSP)),1,0) \
		N64PSP_QUEUE_TRACE=$(N64PSP_QUEUE_TRACE) \
		N64PSP_USE_VFPU=$(N64PSP_USE_VFPU) \
		$(N64PSP_BUILD_TARGETS)
	@touch $@

$(N64PSP_PSP_ARCHIVES): $(N64PSP_BUILD_STAMP)

$(COMPILE_FLAGS_STAMP): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(CFLAGS)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

$(BUILD_DIR)/%.o: %.c Makefile src/psp/sources.mk $(COMPILE_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(call print,Compiling:,$<,$@)
	$(V)$(CC) -c $(CFLAGS) $(source_warning_flags) -I$(dir $*) -o $@ $<

$(BUILD_DIR)/src/psp/audio_mixer.o: CFLAGS += -std=gnu99

$(BUILD_DIR)/%.o: %.S Makefile src/psp/sources.mk $(COMPILE_FLAGS_STAMP)
	@mkdir -p $(dir $@)
	$(call print,Assembling:,$<,$@)
	$(V)$(CC) -c $(CFLAGS) -I$(dir $*) -o $@ $<

clean:
	$(RM) -r $(BUILD_DIR) build/psp-bootstrap
ifneq ($(wildcard $(N64PSP_DIR)/Makefile),)
	$(MAKE) -C $(N64PSP_DIR) clean
endif

print-%:
	$(info $* is a $(flavor $*) variable set to [$($*)])
	@true

resolve-queue-trace:
	$(PYTHON) tools/psp_resolve_queue_trace.py \
		--log $(QUEUE_TRACE_LOG) \
		--elf $(PSP_ELF) \
		--load-base $(PSP_LOAD_BASE) \
		$(foreach q,$(QUEUE_TRACE_QUEUES),--queue $(q))

-include $(DEP_FILES)

.PHONY: tools-init toolchain torch init decompress extract assets clean-generated resolve-queue-trace psp-profile-gprof psp-profile-phases psp-profile-combined psp-profile-builds psp-profile-artifacts psp-profile-report FORCE
