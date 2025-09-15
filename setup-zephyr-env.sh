#!/bin/bash
# Setup Zephyr development environment

echo "Setting up Zephyr environment..."

# Set Zephyr base and SDK
export ZEPHYR_BASE=/home/rapidreach/work/alnicko-rapidreach-fw/external/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/home/rapidreach/zephyr-sdk-0.17.2

# Source Zephyr environment
if [ -f "$ZEPHYR_BASE/zephyr-env.sh" ]; then
    source "$ZEPHYR_BASE/zephyr-env.sh"
    echo "✓ Zephyr environment loaded"
else
    echo "✗ Zephyr environment script not found at $ZEPHYR_BASE"
    exit 1
fi

# Add toolchain to PATH
export PATH=$ZEPHYR_SDK_INSTALL_DIR/arm-zephyr-eabi/bin:$PATH

# Add west to PATH if available
if [ -f "/home/rapidreach/zephyrproject/.venv/bin/west" ]; then
    export PATH=/home/rapidreach/zephyrproject/.venv/bin:$PATH
fi

# Verify tools
echo ""
echo "Checking tools..."
echo -n "west: "; which west || echo "NOT FOUND"
echo -n "cmake: "; which cmake || echo "NOT FOUND"
echo -n "ninja: "; which ninja || echo "NOT FOUND"
echo -n "arm-zephyr-eabi-gdb: "; which arm-zephyr-eabi-gdb || echo "NOT FOUND"

echo ""
echo "Environment ready!"
echo "To build: west build -p auto -b alnicko_speaker"
echo "To flash: west flash"
echo "To debug: Use VSCode with 'Simple Debug (Connect to Port 50000)'"
