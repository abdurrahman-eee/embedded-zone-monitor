#!/usr/bin/env bash
# setup.sh — Clone FreeRTOS and build the project
set -e

FREERTOS_DIR="FreeRTOS-Kernel"
REPO_URL="https://github.com/FreeRTOS/FreeRTOS-Kernel.git"

echo "=== RTOS Temperature Monitor — Setup ==="

# 1. Clone FreeRTOS Kernel if not present
if [ ! -d "$FREERTOS_DIR" ]; then
    echo "[1/3] Cloning FreeRTOS Kernel..."
    git clone --depth=1 "$REPO_URL" "$FREERTOS_DIR"
else
    echo "[1/3] FreeRTOS Kernel already present. Skipping clone."
fi

# 2. Build
echo "[2/3] Building project..."
make

# 3. Done
echo ""
echo "[3/3] Done! Run with:"
echo "      ./build/rtos_temp_monitor"
