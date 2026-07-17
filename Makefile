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
PROFILE_PHASES ?= 0
PROFILE_CAPTURE_FRAMES ?= 300
PROFILE_FRAME_TRACE ?= 0
PROFILE_FRAME_TRACE_FRAMES ?= 240
PROFILE_COMPONENTS ?= 0
PROFILE_TRIVIAL_REJECTS ?= 0
BATCH_STATE_CACHE ?= 1
VTX_PRECOMPOSED_TRANSFORM ?= 0
USE_LOCAL_PSPGL ?= 0
PSP_LOG ?= 0
PSP_TRACE ?= 0
PSP_RENDERER_DIAGNOSTICS ?= 0
ifeq ($(PSP_RENDERER_DIAGNOSTICS),1)
PSP_LOG := 1
endif
PSP_AUDIO ?= 0
PSP_FPS_OVERLAY ?= 1
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

ifneq ($(PSP_AUDIO),0)
ifneq ($(PSP_AUDIO),1)
$(error PSP_AUDIO must be 0 or 1)
endif
endif
ifneq ($(VTX_PRECOMPOSED_TRANSFORM),0)
ifneq ($(VTX_PRECOMPOSED_TRANSFORM),1)
$(error VTX_PRECOMPOSED_TRANSFORM must be 0 or 1)
endif
endif

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
PSPGL_DIR ?= lib/pspgl
LOCAL_PSPGL_LIB := $(PSPGL_DIR)/libGL.a

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
ifeq ($(USE_LOCAL_PSPGL),1)
IINC += -I$(PSPGL_DIR)
endif
IINC += -I$(PSPDEV)/psp/include -I$(PSPSDK)/include
IINC += -Ilib/n64psp/include

PSP_WARNINGS := -Wall -Wextra -Wimplicit-fallthrough -Wno-unknown-pragmas -Wno-missing-braces
PSP_WARNINGS += -Wno-sign-compare -Wno-uninitialized

QUIET_DECOMP ?= 1

define source_warning_flags
$(if $(filter src/psp/%,$<),$(PSP_WARNINGS),$(if $(filter 1,$(QUIET_DECOMP)),-w,$(PSP_WARNINGS)))
endef

PSP_OPTFLAGS ?= -O2 # -ffast-math
SF64_GIT_SHA := $(shell git rev-parse HEAD 2>/dev/null || echo unknown)
N64PSP_GIT_SHA := $(shell git -C lib/n64psp rev-parse HEAD 2>/dev/null || echo unknown)
PSPGL_GIT_SHA := $(shell git -C $(PSPGL_DIR) rev-parse HEAD 2>/dev/null || echo unknown)
SF64_GIT_DIRTY := $(shell test -z "$$(git status --porcelain 2>/dev/null)" && echo clean || echo dirty)
N64PSP_GIT_DIRTY := $(shell test -z "$$(git -C lib/n64psp status --porcelain 2>/dev/null)" && echo clean || echo dirty)
PSPGL_GIT_DIRTY := $(shell test -z "$$(git -C $(PSPGL_DIR) status --porcelain 2>/dev/null)" && echo clean || echo dirty)
PSPGL_SOURCE_MODE := $(if $(filter 1,$(USE_LOCAL_PSPGL)),local,system)
PSPGL_PROFILE_GIT_SHA := $(if $(filter 1,$(USE_LOCAL_PSPGL)),$(PSPGL_GIT_SHA),system)
PSPGL_PROFILE_GIT_DIRTY := $(if $(filter 1,$(USE_LOCAL_PSPGL)),$(PSPGL_GIT_DIRTY),system)
PERFECT_DARK_PSP_SHA := 0871c907aea105cd2e7002219d047c733011f668
PSP_COMPILER_VERSION := $(shell $(CC) --version 2>/dev/null | sed -n '1p' | sed 's/"/\\"/g; s/[[:space:]]\+/_/g')

ifeq ($(PROFILE_FRAME_TRACE),1)
ifneq ($(PROFILE_PHASES),1)
$(error PROFILE_FRAME_TRACE=1 requires PROFILE_PHASES=1.)
endif
ifeq ($(PROFILE_FRAME_TRACE_FRAMES),0)
$(error PROFILE_FRAME_TRACE_FRAMES must be at least 1.)
endif
ifneq ($(shell test "$(PROFILE_FRAME_TRACE_FRAMES)" -le 3600 2>/dev/null && echo ok),ok)
$(error PROFILE_FRAME_TRACE_FRAMES=$(PROFILE_FRAME_TRACE_FRAMES) is unreasonable; use 1..3600.)
endif
endif
ifeq ($(PROFILE_COMPONENTS),1)
ifneq ($(PROFILE_PHASES),1)
$(error PROFILE_COMPONENTS=1 requires PROFILE_PHASES=1.)
endif
endif
ifeq ($(PROFILE_TRIVIAL_REJECTS),1)
ifneq ($(PROFILE_PHASES),1)
$(error PROFILE_TRIVIAL_REJECTS=1 requires PROFILE_PHASES=1.)
endif
endif
ifeq ($(PROFILE_PSP),1)
ifeq ($(PROFILE_PHASES),1)
PSP_PROFILE_MODE := combined
else
PSP_PROFILE_MODE := gprof
endif
else ifeq ($(PROFILE_PHASES),1)
PSP_PROFILE_MODE := phases
else
PSP_PROFILE_MODE := release
endif

ifeq ($(filter 1,$(PROFILE_PSP) $(PROFILE_PHASES)),)
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
CFLAGS += -DPSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY)
CFLAGS += -DPSP_AUDIO=$(PSP_AUDIO)
CFLAGS += -DPROFILE_GPROF=$(if $(filter 1,$(PROFILE_PSP)),1,0)
CFLAGS += -DPROFILE_PHASES=$(if $(filter 1,$(PROFILE_PHASES)),1,0)
CFLAGS += -DPROFILE_CAPTURE_FRAMES=$(PROFILE_CAPTURE_FRAMES)
CFLAGS += -DPROFILE_FRAME_TRACE=$(if $(filter 1,$(PROFILE_FRAME_TRACE)),1,0)
CFLAGS += -DPROFILE_FRAME_TRACE_FRAMES=$(PROFILE_FRAME_TRACE_FRAMES)
CFLAGS += -DPROFILE_COMPONENTS=$(if $(filter 1,$(PROFILE_COMPONENTS)),1,0)
CFLAGS += -DPROFILE_TRIVIAL_REJECTS=$(if $(filter 1,$(PROFILE_TRIVIAL_REJECTS)),1,0)
CFLAGS += -DBATCH_STATE_CACHE=$(if $(filter 1,$(BATCH_STATE_CACHE)),1,0)
CFLAGS += -DVTX_PRECOMPOSED_TRANSFORM=$(if $(filter 1,$(VTX_PRECOMPOSED_TRANSFORM)),1,0)
CFLAGS += '-DSF64_GIT_SHA="$(SF64_GIT_SHA)"'
CFLAGS += '-DN64PSP_GIT_SHA="$(N64PSP_GIT_SHA)"'
CFLAGS += '-DPSPGL_GIT_SHA="$(PSPGL_PROFILE_GIT_SHA)"'
CFLAGS += '-DPSPGL_GIT_DIRTY="$(PSPGL_PROFILE_GIT_DIRTY)"'
CFLAGS += '-DPSPGL_SOURCE_MODE="$(PSPGL_SOURCE_MODE)"'
CFLAGS += '-DPERFECT_DARK_PSP_SHA="$(PERFECT_DARK_PSP_SHA)"'
CFLAGS += '-DBUILD_COMPILER="$(PSP_COMPILER_VERSION)"'
CFLAGS += '-DBUILD_OPT_FLAGS="$(PSP_OPTFLAGS)"'
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
ifeq ($(USE_LOCAL_PSPGL),1)
PSPGL_HEADER := $(firstword $(wildcard $(PSPGL_DIR)/GLES/egl.h) $(wildcard $(PSPGL_DIR)/GL/gl.h))
ifeq ($(PSPGL_HEADER),)
$(error Local PSPGL headers not found under $(PSPGL_DIR). Update lib/pspgl or build without USE_LOCAL_PSPGL=1.)
endif
PSPGL_CFLAGS :=
PSPGL_LIBS := $(LOCAL_PSPGL_LIB) -lpspvfpu
PSPGL_BUILD_DEPS := $(LOCAL_PSPGL_LIB)
else
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
PSPGL_CFLAGS :=
PSPGL_LIBS := -lGL -lpspvfpu
endif
PSPGL_BUILD_DEPS :=
endif
CFLAGS += $(PSPGL_CFLAGS)
PSP_LIBS := $(PSPGL_LIBS) $(PSP_LIBS)

N64PSP_DIR := lib/n64psp
N64PSP_BUILD_PSP := build-psp-$(PSP_PROFILE_MODE)

N64PSP_BUILD_STAMP := $(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/.sf64-n64psp-build.stamp

N64PSP_PSP_ARCHIVES := \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_runtime.a \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_trace_backend.a \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_platform_psp.a \
	$(N64PSP_DIR)/$(N64PSP_BUILD_PSP)/libn64psp_math.a

N64PSP_BUILD_TARGETS := \
	$(N64PSP_BUILD_PSP)/libn64psp_runtime.a \
	$(N64PSP_BUILD_PSP)/libn64psp_trace_backend.a \
	$(N64PSP_BUILD_PSP)/libn64psp_platform_psp.a \
	$(N64PSP_BUILD_PSP)/libn64psp_math.a

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
O_FILES += $(N64PSP_PSP_ARCHIVES)
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

psp-profile-gprof:
	$(MAKE) PROFILE_PSP=1 PROFILE_PHASES=0 BUILD_DIR=build/psp-profile-gprof PSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY) PSP_LOG=$(PSP_LOG) PSP_TRACE=$(PSP_TRACE) PSP_RENDERER_DIAGNOSTICS=$(PSP_RENDERER_DIAGNOSTICS) psp

psp-profile-phases:
	$(MAKE) PROFILE_PSP=0 PROFILE_PHASES=1 BUILD_DIR=build/psp-profile-phases PSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY) PSP_LOG=$(PSP_LOG) PSP_TRACE=$(PSP_TRACE) PSP_RENDERER_DIAGNOSTICS=$(PSP_RENDERER_DIAGNOSTICS) psp

psp-profile-combined:
	$(MAKE) PROFILE_PSP=1 PROFILE_PHASES=1 BUILD_DIR=build/psp-profile-combined PSP_FPS_OVERLAY=$(PSP_FPS_OVERLAY) PSP_LOG=$(PSP_LOG) PSP_TRACE=$(PSP_TRACE) PSP_RENDERER_DIAGNOSTICS=$(PSP_RENDERER_DIAGNOSTICS) psp

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
		printf 'pspgl_source=%s\n' '$(if $(filter 1,$(USE_LOCAL_PSPGL)),local,system)'; \
		printf 'pspgl_commit=%s\n' '$(if $(filter 1,$(USE_LOCAL_PSPGL)),$(PSPGL_GIT_SHA),system)'; \
		printf 'pspgl_worktree=%s\n' '$(if $(filter 1,$(USE_LOCAL_PSPGL)),$(PSPGL_GIT_DIRTY),system)'; \
		printf 'compiler=%s\n' '$(PSP_COMPILER_VERSION)'; \
		printf 'cflags=%s\n' '$(CFLAGS)'; \
		printf 'ldflags=%s\n' '$(LDFLAGS)'; \
		printf 'psp_libs=%s\n' '$(PSP_LIBS)'; \
		printf 'n64psp_build_dir=%s\n' '$(N64PSP_BUILD_PSP)'; \
		printf 'PROFILE_PSP=%s\n' '$(PROFILE_PSP)'; \
		printf 'PROFILE_PHASES=%s\n' '$(PROFILE_PHASES)'; \
		printf 'PROFILE_CAPTURE_FRAMES=%s\n' '$(PROFILE_CAPTURE_FRAMES)'; \
		printf 'PROFILE_FRAME_TRACE=%s\n' '$(PROFILE_FRAME_TRACE)'; \
		printf 'PROFILE_FRAME_TRACE_FRAMES=%s\n' '$(PROFILE_FRAME_TRACE_FRAMES)'; \
		printf 'PROFILE_COMPONENTS=%s\n' '$(PROFILE_COMPONENTS)'; \
		printf 'PROFILE_TRIVIAL_REJECTS=%s\n' '$(PROFILE_TRIVIAL_REJECTS)'; \
		printf 'USE_LOCAL_PSPGL=%s\n' '$(USE_LOCAL_PSPGL)'; \
		printf 'BATCH_STATE_CACHE=%s\n' '$(BATCH_STATE_CACHE)'; \
		printf 'VTX_PRECOMPOSED_TRANSFORM=%s\n' '$(VTX_PRECOMPOSED_TRANSFORM)'; \
		printf 'PSP_AUDIO=%s\n' '$(PSP_AUDIO)'; \
		printf 'PSP_FPS_OVERLAY=%s\n' '$(PSP_FPS_OVERLAY)'; \
		printf 'PSP_LOG=%s\n' '$(PSP_LOG)'; \
		printf 'PSP_TRACE=%s\n' '$(PSP_TRACE)'; \
		printf 'PSP_RENDERER_DIAGNOSTICS=%s\n' '$(PSP_RENDERER_DIAGNOSTICS)'; \
		printf 'cpu_clock_runtime=recorded in profile-NNN.txt on PSP\n'; \
		printf 'bus_clock_runtime=recorded in profile-NNN.txt on PSP\n'; \
		printf 'build_command=make %s BUILD_DIR=%s PROFILE_PSP=%s PROFILE_PHASES=%s PROFILE_CAPTURE_FRAMES=%s PROFILE_FRAME_TRACE=%s PROFILE_FRAME_TRACE_FRAMES=%s PROFILE_COMPONENTS=%s PROFILE_TRIVIAL_REJECTS=%s USE_LOCAL_PSPGL=%s BATCH_STATE_CACHE=%s VTX_PRECOMPOSED_TRANSFORM=%s PSP_AUDIO=%s PSP_FPS_OVERLAY=%s PSP_LOG=%s PSP_TRACE=%s PSP_RENDERER_DIAGNOSTICS=%s psp\n' '$(MAKECMDGOALS)' '$(BUILD_DIR)' '$(PROFILE_PSP)' '$(PROFILE_PHASES)' '$(PROFILE_CAPTURE_FRAMES)' '$(PROFILE_FRAME_TRACE)' '$(PROFILE_FRAME_TRACE_FRAMES)' '$(PROFILE_COMPONENTS)' '$(PROFILE_TRIVIAL_REJECTS)' '$(USE_LOCAL_PSPGL)' '$(BATCH_STATE_CACHE)' '$(VTX_PRECOMPOSED_TRANSFORM)' '$(PSP_AUDIO)' '$(PSP_FPS_OVERLAY)' '$(PSP_LOG)' '$(PSP_TRACE)' '$(PSP_RENDERER_DIAGNOSTICS)'; \
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

$(PSP_ELF): $(O_FILES) $(PSPGL_BUILD_DEPS)
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
		$(N64PSP_BUILD_TARGETS)
	@touch $@

$(N64PSP_PSP_ARCHIVES): $(N64PSP_BUILD_STAMP)

$(LOCAL_PSPGL_LIB): FORCE
	env -u MAKEFLAGS $(MAKE) -C $(PSPGL_DIR) .deps libGL.a \
		PSPPATH=$(PSPDEV)/psp \
		PSPSDK=$(PSPSDK) \
		ARCH=psp- \
		CC="$(CC) -std=gnu99" \
		AR=psp-ar \
		RANLIB=psp-ranlib

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
