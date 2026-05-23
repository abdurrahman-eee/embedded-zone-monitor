# ============================================================
#  Makefile — RTOS Temperature Monitor (FreeRTOS POSIX/macOS)
# ============================================================

CC      = gcc
TARGET  = build/rtos_temp_monitor
BUILDDIR= build

FREERTOS = FreeRTOS-Kernel

# --- Include paths ---
INCLUDES = \
    -I$(FREERTOS)/include \
    -I$(FREERTOS)/portable/ThirdParty/GCC/Posix \
    -I$(FREERTOS)/portable/ThirdParty/GCC/Posix/utils \
    -Isrc

# --- FreeRTOS source files ---
FREERTOS_SRC = \
    $(FREERTOS)/tasks.c \
    $(FREERTOS)/queue.c \
    $(FREERTOS)/list.c \
    $(FREERTOS)/timers.c \
    $(FREERTOS)/event_groups.c \
    $(FREERTOS)/portable/ThirdParty/GCC/Posix/port.c \
    $(FREERTOS)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
    $(FREERTOS)/portable/MemMang/heap_3.c

# --- App source files ---
APP_SRC = \
    src/main.c \
    src/hardware_sim.c

# --- Compiler flags ---
CFLAGS = -Wall -Wextra -O0 -g \
         -DprojCOVERAGE_TEST=0 \
         $(INCLUDES)

LDFLAGS = -lpthread -lm

# --- Build rules ---
.PHONY: all clean run

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(FREERTOS_SRC) $(APP_SRC) src/FreeRTOSConfig.h
	$(CC) $(CFLAGS) $(FREERTOS_SRC) $(APP_SRC) -o $@ $(LDFLAGS)
	@echo "Build successful: $@"

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILDDIR)
