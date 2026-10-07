# Симулятор интерфейса на ПК: LVGL + Core/Src/ui (без lv_port_*.c) + заглушки.
# Обычно вызывается из build.sh. Компилятор — MinGW gcc хоста.

ROOT    := ../..
CC      ?= gcc
BUILD   := build
TARGET  := $(BUILD)/ui_sim.exe

LVGL_DIR := $(ROOT)/Middlewares/Third_Party/lvgl
UI_DIR   := $(ROOT)/Core/Src/ui

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$(d),$(2)) $(filter $(subst *,%,$(2)),$(d)))

LVGL_SRC := $(call rwildcard,$(LVGL_DIR)/src,*.c)
UI_SRC   := $(filter-out $(UI_DIR)/lv_port_%.c,$(call rwildcard,$(UI_DIR),*.c))
SIM_SRC  := sim_main.c sim_port.c sim_app.c

INC  := -I. -I$(ROOT)/Core/Inc -I$(UI_DIR) -I$(LVGL_DIR) -I$(ROOT)/Middlewares/Third_Party
DEFS := -DLV_CONF_INCLUDE_SIMPLE

# -m32: указатели 4 байта, как на Cortex-M4 — расход кучи LVGL как в прошивке
ARCH := -m32
CFLAGS_COMMON := $(ARCH) -std=gnu11 -O1 -g $(DEFS) $(INC) -MMD -MP
# Свой код (UI и симулятор) — со всеми предупреждениями
CFLAGS_UI   := $(CFLAGS_COMMON) -Wall -Wextra -Wshadow -Wno-unused-parameter
# LVGL — как есть (предупреждения LVGL не наши)
CFLAGS_LVGL := $(CFLAGS_COMMON) -w


LVGL_OBJ := $(patsubst $(ROOT)/%.c,$(BUILD)/root/%.o,$(LVGL_SRC))
UI_OBJ   := $(patsubst $(ROOT)/%.c,$(BUILD)/root/%.o,$(UI_SRC))
SIM_OBJ  := $(patsubst %.c,$(BUILD)/sim/%.o,$(SIM_SRC))

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(LVGL_OBJ) $(UI_OBJ) $(SIM_OBJ)
	$(file >$(BUILD)/objects.list,$^)
	$(CC) $(ARCH) @$(BUILD)/objects.list -o $@

$(LVGL_OBJ): $(BUILD)/root/%.o: $(ROOT)/%.c
	@mkdir -p $(@D)
	@$(CC) -c $(CFLAGS_LVGL) $< -o $@

$(UI_OBJ): $(BUILD)/root/%.o: $(ROOT)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS_UI) $< -o $@

$(SIM_OBJ): $(BUILD)/sim/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS_UI) $< -o $@

clean:
	rm -rf $(BUILD)

-include $(LVGL_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(SIM_OBJ:.o=.d)
