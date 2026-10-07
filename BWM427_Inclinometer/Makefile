##############################################################################
# Command-line build for BWM427_Inclinometer (STM32F411CEUx, HAL + FatFs [+ LVGL])
#
# Mirrors the STM32CubeIDE "Debug" configuration (.cproject), but builds into
# build/ (Debug/ belongs to CubeIDE).
#
#   make -j8                 build (default target: all)
#   make -j8 UI=0            build without LVGL and without Core/Src/ui/**
#   make clean               remove build/
#   make V=1                 show full compiler command lines
#   make GCC_PATH=...        use another toolchain bin dir (empty = use PATH)
#   make OPT=-O0             override optimisation (CubeIDE Debug uses -O0)
#   make print-VAR           print the value of a make variable (debug aid)
#
# Works with mingw make (GNU Make 4.x) from Git Bash (POSIX sh) and from
# PowerShell / cmd (make falls back to cmd.exe when no sh.exe is on PATH).
#
# Notes:
#  - Switching UI=0 <-> UI=1 (or OPT, EXTRA_CFLAGS...) in the same build dir
#    triggers a full rebuild. Two people/configs building at the same time
#    should use separate dirs, e.g.  make UI=0 BUILD_DIR=build_noui
#  - With LVGL, keep the project directory path short (<= ~80 chars): GCC
#    passes un-normalised include paths to Windows, which has a 260-char limit.
##############################################################################

TARGET    := BWM427_Inclinometer
BUILD_DIR ?= build

# UI=1 (default): if Core/Inc/lv_conf.h exists, compile LVGL + Core/Src/ui/**
# UI=0          : never compile LVGL or anything under Core/Src/ui/
UI ?= 1

OPT         ?= -Os
DEBUG_FLAGS ?= -g3
V           ?= 0

# binutils >= 2.39 warns "LOAD segment with RWX permissions" for the CubeMX
# linker script (harmless on Cortex-M). Set empty for older toolchains, e.g.
# CubeIDE's GCC 10.3: make LD_NO_RWX_WARN=
LD_NO_RWX_WARN ?= -Wl,--no-warn-rwx-segments

##############################################################################
# Toolchain
##############################################################################
GCC_PATH ?= C:/Tools/arm-gnu-toolchain/bin
PREFIX   ?= arm-none-eabi-

ifeq ($(strip $(GCC_PATH)),)
  TOOL = $(PREFIX)$(1)
else
  TOOL = "$(GCC_PATH)/$(PREFIX)$(1)"
endif

CC := $(call TOOL,gcc)
AS := $(call TOOL,gcc) -x assembler-with-cpp
CP := $(call TOOL,objcopy)
SZ := $(call TOOL,size)

##############################################################################
# Shell abstraction (Git Bash sh vs. cmd.exe fallback)
##############################################################################
ifeq ($(OS),Windows_NT)
  ifeq ($(SHELL),sh.exe)
    # mingw make found no sh.exe on PATH and runs recipes with cmd.exe
    WIN_CMD := 1
  endif
endif

ifdef WIN_CMD
  winpath = $(subst /,\,$(1))
  MKDIR   = if not exist "$(call winpath,$(1))" mkdir "$(call winpath,$(1))"
  RMDIR   = if exist "$(call winpath,$(1))" rmdir /s /q "$(call winpath,$(1))"
else
  MKDIR   = mkdir -p "$(1)"
  RMDIR   = rm -rf "$(1)"
endif

ifeq ($(V),1)
  Q :=
  echo_cmd =
else
  Q := @
  echo_cmd = @echo $(1)
endif

# Recursive wildcard: $(call rwildcard,<dir>,<pattern>) - no `find` needed
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$(d),$(2)) $(filter $(subst *,%,$(2)),$(d)))

##############################################################################
# Sources
##############################################################################
C_SOURCES := $(wildcard Core/Src/*.c)
C_SOURCES += $(filter-out %_template.c,$(wildcard Drivers/STM32F4xx_HAL_Driver/Src/*.c))
C_SOURCES += $(wildcard FATFS/App/*.c)
C_SOURCES += $(wildcard FATFS/Target/*.c)
# Same FatFs set CubeIDE compiles (Debug/objects.list). ccsbcs.c is a separate
# translation unit here (provides ff_convert/ff_wtoupper for _USE_LFN).
C_SOURCES += $(wildcard $(addprefix Middlewares/Third_Party/FatFs/src/,\
               ff.c diskio.c ff_gen_drv.c option/syscall.c option/ccsbcs.c))
# USB CDC device (virtual COM port, Core/Src/usb_cli.c): CubeMX-style glue in
# USB_DEVICE/ + ST USB Device Library (Core + CDC class only, no templates).
USB_LIB_DIR := Middlewares/ST/STM32_USB_Device_Library
C_SOURCES += $(wildcard USB_DEVICE/App/*.c)
C_SOURCES += $(wildcard USB_DEVICE/Target/*.c)
C_SOURCES += $(filter-out %_template.c,$(wildcard $(USB_LIB_DIR)/Core/Src/*.c))
C_SOURCES += $(filter-out %_template.c,$(wildcard $(USB_LIB_DIR)/Class/CDC/Src/*.c))

ASM_SOURCES := Core/Startup/startup_stm32f411ceux.s

LDSCRIPT := STM32F411CEUX_FLASH.ld

##############################################################################
# Includes / defines
##############################################################################
C_DEFS := -DUSE_HAL_DRIVER -DSTM32F411xE -DDEBUG

C_INCLUDES := \
  -ICore/Inc \
  -IDrivers/STM32F4xx_HAL_Driver/Inc \
  -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
  -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
  -IDrivers/CMSIS/Include \
  -IFATFS/App \
  -IFATFS/Target \
  -IMiddlewares/Third_Party/FatFs/src \
  -IUSB_DEVICE/App \
  -IUSB_DEVICE/Target \
  -I$(USB_LIB_DIR)/Core/Inc \
  -I$(USB_LIB_DIR)/Class/CDC/Inc

##############################################################################
# Optional LVGL + UI
##############################################################################
LVGL_DIR := Middlewares/Third_Party/lvgl
USE_LVGL :=

# LVGL source directories NOT compiled (override with LVGL_EXCLUDE_DIRS= to
# compile everything). libs/gltf needs C++ + OpenGL ES (its .cpp parts are not
# built here) and has the longest include chains in LVGL: GCC hands Windows
# un-normalised "a/../../b/../c" paths, which hit MAX_PATH (260 chars) once the
# project directory path is longer than ~64 chars (~81 without gltf).
# Matching is case-insensitive because mingw make's wildcard may change letter
# case (lvgl/src/drivers comes back as ".../src/Drivers" - harmless on NTFS).
LVGL_EXCLUDE_DIRS ?= $(LVGL_DIR)/src/libs/gltf

lc = $(subst A,a,$(subst B,b,$(subst C,c,$(subst D,d,$(subst E,e,$(subst F,f,\
     $(subst G,g,$(subst H,h,$(subst I,i,$(subst J,j,$(subst K,k,$(subst L,l,\
     $(subst M,m,$(subst N,n,$(subst O,o,$(subst P,p,$(subst Q,q,$(subst R,r,\
     $(subst S,s,$(subst T,t,$(subst U,u,$(subst V,v,$(subst W,w,$(subst X,x,\
     $(subst Y,y,$(subst Z,z,$(1)))))))))))))))))))))))))))

ifneq ($(strip $(UI)),0)
  ifneq ($(wildcard Core/Inc/lv_conf.h),)
    USE_LVGL := 1
  endif
endif

LVGL_SOURCES :=
ifeq ($(USE_LVGL),1)
  ifeq ($(wildcard $(LVGL_DIR)/src),)
    $(error Core/Inc/lv_conf.h exists but LVGL was not found in $(LVGL_DIR)/src (build with UI=0 to skip the UI))
  endif
  LVGL_ALL_SOURCES := $(call rwildcard,$(LVGL_DIR)/src,*.c)
  ifeq ($(strip $(LVGL_EXCLUDE_DIRS)),)
    LVGL_SOURCES := $(LVGL_ALL_SOURCES)
  else
    LVGL_EXCLUDE_LC := $(call lc,$(addsuffix /%,$(patsubst %/,%,$(LVGL_EXCLUDE_DIRS))))
    LVGL_SOURCES := $(foreach f,$(LVGL_ALL_SOURCES),\
                      $(if $(filter $(LVGL_EXCLUDE_LC),$(call lc,$(f))),,$(f)))
  endif
  C_SOURCES  += $(LVGL_SOURCES)
  C_SOURCES  += $(call rwildcard,Core/Src/ui,*.c)
  C_INCLUDES += -I$(LVGL_DIR) -IMiddlewares/Third_Party -ICore/Src/ui
  C_DEFS     += -DLV_CONF_INCLUDE_SIMPLE
endif

##############################################################################
# Flags
##############################################################################
MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

DEPFLAGS = -MMD -MP

CFLAGS  = $(MCU) -std=gnu11 $(C_DEFS) $(C_INCLUDES) $(OPT) $(DEBUG_FLAGS) \
          -ffunction-sections -fdata-sections -Wall --specs=nano.specs \
          $(DEPFLAGS) $(EXTRA_CFLAGS)

ASFLAGS = $(MCU) $(DEBUG_FLAGS) -DDEBUG --specs=nano.specs -MMD -MP

LDFLAGS = $(MCU) -T$(LDSCRIPT) --specs=nosys.specs --specs=nano.specs \
          -u _printf_float -static \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -Wl,--gc-sections \
          -Wl,--print-memory-usage $(LD_NO_RWX_WARN) \
          -Wl,--start-group -lc -lm -Wl,--end-group $(EXTRA_LDFLAGS)

##############################################################################
# Objects (mirrored source tree under build/, so same-named files never clash)
##############################################################################
C_OBJECTS    := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS  := $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
LVGL_OBJECTS := $(addprefix $(BUILD_DIR)/,$(LVGL_SOURCES:.c=.o))
OBJECTS      := $(C_OBJECTS) $(ASM_OBJECTS)

ELF := $(BUILD_DIR)/$(TARGET).elf
HEX := $(BUILD_DIR)/$(TARGET).hex
BIN := $(BUILD_DIR)/$(TARGET).bin

##############################################################################
# Configuration stamps. Objects depend on build/.cflags (compiler + flags),
# the ELF on build/.ldflags (linker flags + object list). Each stamp is
# rewritten - and so becomes newer than its dependants - only when its content
# changes, e.g. switching UI=0 <-> UI=1, OPT=..., EXTRA_CFLAGS=..., or adding
# a source file (relink only). Hence no stale objects/ELF across such switches.
##############################################################################
CFLAGS_STAMP := $(BUILD_DIR)/.cflags
LINK_STAMP   := $(BUILD_DIR)/.ldflags
CFLAGS_NOW   := $(strip $(CC) $(CFLAGS) | $(AS) $(ASFLAGS))
LINK_NOW     := $(strip $(CC) $(LDFLAGS) | $(CP) | $(OBJECTS))

ifeq ($(filter clean print-%,$(MAKECMDGOALS)),)
  ifneq ($(wildcard $(CFLAGS_STAMP)),)
    ifneq ($(strip $(file <$(CFLAGS_STAMP))),$(CFLAGS_NOW))
      _ := $(file >$(CFLAGS_STAMP),$(CFLAGS_NOW))
    endif
  endif
  ifneq ($(wildcard $(LINK_STAMP)),)
    ifneq ($(strip $(file <$(LINK_STAMP))),$(LINK_NOW))
      _ := $(file >$(LINK_STAMP),$(LINK_NOW))
    endif
  endif
endif

##############################################################################
# Rules
##############################################################################
.PHONY: all clean
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.SECONDEXPANSION:

all: $(ELF) $(HEX) $(BIN)
	$(Q)$(SZ) $(ELF)

$(BUILD_DIR)/%.o: %.c $(CFLAGS_STAMP) | $$(@D)/.
	$(call echo_cmd,CC  $<)
	$(Q)$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s $(CFLAGS_STAMP) | $$(@D)/.
	$(call echo_cmd,AS  $<)
	$(Q)$(AS) -c $(ASFLAGS) $< -o $@

# LVGL objects get no per-file header dependencies: each LVGL .c pulls in ~800
# (un-normalised) header paths and reading/stat-ing them made a no-op build take
# ~17 s instead of <1 s. The vendored LVGL headers do not change, so LVGL is
# rebuilt only when lv_conf.h, the LVGL version header or the compile flags
# change (after hand-editing LVGL headers: make clean). Core/Src/ui and all
# project sources keep full header dependencies.
ifneq ($(LVGL_OBJECTS),)
$(LVGL_OBJECTS): DEPFLAGS :=
$(LVGL_OBJECTS): Core/Inc/lv_conf.h $(wildcard $(LVGL_DIR)/include/lvgl/lv_version.h)
endif

$(CFLAGS_STAMP): | $(BUILD_DIR)/.
	$(file >$@,$(CFLAGS_NOW))

$(LINK_STAMP): | $(BUILD_DIR)/.
	$(file >$@,$(LINK_NOW))

# The object list goes through a response file: with LVGL there are ~500
# objects and the command line would exceed the Windows length limit.
$(ELF): $(OBJECTS) $(LDSCRIPT) $(LINK_STAMP) | $(BUILD_DIR)/.
	$(file >$(BUILD_DIR)/objects.list,$(OBJECTS))
	$(call echo_cmd,LD  $@)
	$(Q)$(CC) @$(BUILD_DIR)/objects.list $(LDFLAGS) -o $@

# `ver` over USB reports __DATE__/__TIME__ of usb_cli.c: recompile it whenever
# any other object is rebuilt, so the stamp is the time of the firmware build.
USB_CLI_OBJ := $(BUILD_DIR)/Core/Src/usb_cli.o
ifneq ($(filter $(USB_CLI_OBJ),$(OBJECTS)),)
$(USB_CLI_OBJ): $(filter-out $(USB_CLI_OBJ),$(OBJECTS))
endif

$(HEX): $(ELF)
	$(call echo_cmd,HEX $@)
	$(Q)$(CP) -O ihex $< $@

$(BIN): $(ELF)
	$(call echo_cmd,BIN $@)
	$(Q)$(CP) -O binary -S $< $@

# Directory creation: every object depends (order-only) on "<its dir>/.",
# so each directory is created exactly once, which is safe under make -j.
.PRECIOUS: %/.
%/.:
	$(Q)$(call MKDIR,$*)

clean:
	$(call RMDIR,$(BUILD_DIR))

print-%:
	@echo $* = $($*)

-include $(patsubst %.o,%.d,$(filter-out $(LVGL_OBJECTS),$(OBJECTS)))
